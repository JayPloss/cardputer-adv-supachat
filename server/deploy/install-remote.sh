#!/usr/bin/env bash
set -euo pipefail

archive=/tmp/supachat-server.tar.gz
env_source=/tmp/supachat.env
install_root=/opt/supachat/server
data_root=/var/lib/supachat

id -u supachat >/dev/null 2>&1 || useradd --system --home-dir "$data_root" --shell /usr/sbin/nologin supachat
install -d -o root -g root -m 0755 /opt/supachat "$install_root"
install -d -o supachat -g supachat -m 0750 "$data_root"

staging=$(mktemp -d /opt/supachat/deploy.XXXXXX)
trap 'rm -rf "$staging"' EXIT
tar -xzf "$archive" -C "$staging"
find "$install_root" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
cp -a "$staging"/. "$install_root"/
chown -R root:root "$install_root"
find "$install_root" -type d -exec chmod 0755 {} +
find "$install_root" -type f -exec chmod 0644 {} +

install -o root -g root -m 0600 "$env_source" /etc/supachat.env
install -o root -g root -m 0644 "$install_root/deploy/supachat.service" /etc/systemd/system/supachat.service

authentik_server=$(docker ps --filter label=com.docker.compose.service=authentik-server --format '{{.Names}}' | head -n 1)
if [[ -z "$authentik_server" ]]; then echo 'Authentik server container not found by service label.' >&2; exit 1; fi
docker network inspect supachat-auth >/dev/null 2>&1 || docker network create --driver bridge supachat-auth >/dev/null
docker exec -u 0 "$authentik_server" install -d -o root -g root -m 0755 /data/media/public/branding
docker cp "$install_root/web/supachat-logo.png" "$authentik_server:/data/media/public/branding/supachat-logo.png"
docker exec -u 0 "$authentik_server" chmod 0644 /data/media/public/branding/supachat-logo.png
docker exec -i "$authentik_server" ak shell < "$install_root/deploy/configure-authentik.py"
invite_config=$(docker exec -i "$authentik_server" ak shell < "$install_root/deploy/configure-invite-service.py" | grep '^SUPACHAT_AUTHENTIK_')
sed -i '/^SUPACHAT_AUTHENTIK_/d' /etc/supachat.env
printf '%s\n' "$invite_config" >> /etc/supachat.env

for attempt in $(seq 1 30); do
  if curl --fail --silent http://127.0.0.1:9000/outpost.goauthentik.io/ping >/dev/null; then break; fi
  if [[ "$attempt" == 30 ]]; then docker ps --filter name=ak-outpost --no-trunc; exit 1; fi
  sleep 2
done

cp -a /etc/caddy/Caddyfile "/etc/caddy/Caddyfile.before-supachat.$(date +%Y%m%d%H%M%S)"
python3 "$install_root/deploy/patch-caddy.py" /etc/caddy/Caddyfile
caddy validate --config /etc/caddy/Caddyfile

systemctl daemon-reload
systemctl enable --now supachat.service
systemctl restart supachat.service
for attempt in $(seq 1 20); do
  if curl --fail --silent http://127.0.0.1:8094/healthz >/dev/null; then break; fi
  if [[ "$attempt" == 20 ]]; then journalctl -u supachat.service -n 50 --no-pager; exit 1; fi
  sleep 1
done
caddy reload --config /etc/caddy/Caddyfile
curl --fail --silent http://127.0.0.1:8094/healthz
