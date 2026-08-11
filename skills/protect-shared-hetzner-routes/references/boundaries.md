# SupaChat server boundaries

- Public host: `le954.ca` on shared Hetzner infrastructure.
- SupaChat owns `/supachat`, `/supachat/api/...`, and `/supachat/walkie` only.
- Portal login and Cardputer bearer tokens are separate auth paths.
- Firmware certificate pins are client-side and must not require shared Caddy or auth changes.
- Before a mutation, record health for SupaChat and at least one unrelated existing application route.
