# SupaChat server boundaries

- Public host: `supachat.net` on shared Hetzner infrastructure.
- SupaChat owns the `supachat.net` and `auth.supachat.net` virtual hosts only. It must not install routes on unrelated domains.
- Portal login and Cardputer bearer tokens are separate auth paths.
- Firmware certificate pins are client-side and must not require shared Caddy or auth changes.
- Before a mutation, record health for SupaChat and at least one unrelated existing application route.
