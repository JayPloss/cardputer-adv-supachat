from pathlib import Path
import sys

path = Path(sys.argv[1] if len(sys.argv) > 1 else "/etc/caddy/Caddyfile")
text = path.read_text(encoding="utf-8")
begin = "    # BEGIN SUPACHAT\n"
end = "    # END SUPACHAT\n"
portal_block = """

# BEGIN SUPACHAT PORTAL
supachat.net {
    bind 65.108.148.87 2a01:4f9:c014:a269::1
    encode zstd gzip

    route {
        reverse_proxy /outpost.goauthentik.io/* 127.0.0.1:9000

        @supachat_browser not header Authorization "Bearer *"
        forward_auth @supachat_browser 127.0.0.1:9000 {
            uri /outpost.goauthentik.io/auth/caddy
            copy_headers X-Authentik-Username X-Authentik-Email X-Authentik-Name X-Authentik-Uid
            trusted_proxies private_ranges
        }

        reverse_proxy 127.0.0.1:8094
    }
}

www.supachat.net {
    bind 65.108.148.87 2a01:4f9:c014:a269::1
    redir https://supachat.net{uri} 308
}

auth.supachat.net {
    bind 65.108.148.87 2a01:4f9:c014:a269::1
    encode zstd gzip
    @supachat_oauth_token_no_slash path /application/o/token
    rewrite @supachat_oauth_token_no_slash /application/o/token/
    reverse_proxy 127.0.0.1:9100
}
# END SUPACHAT PORTAL
"""
if begin in text:
    start = text.index(begin)
    finish = text.index(end, start) + len(end)
    while finish < len(text) and text[finish] == "\n":
        finish += 1
    updated = text[:start] + text[finish:]
else:
    updated = text
portal_begin = "# BEGIN SUPACHAT PORTAL\n"
portal_end = "# END SUPACHAT PORTAL\n"
if portal_begin in updated:
    start = updated.index(portal_begin)
    finish = updated.index(portal_end, start) + len(portal_end)
    while finish < len(updated) and updated[finish] == "\n":
        finish += 1
    updated = updated[:start] + portal_block.lstrip("\n") + updated[finish:]
else:
    updated = updated.rstrip() + portal_block + "\n"
path.write_text(updated, encoding="utf-8")
