# SupaChat server boundaries

- Public host: `supachat.net` on shared Hetzner infrastructure.
- SupaChat owns the `supachat.net` and `auth.supachat.net` virtual hosts only. It must not install routes on unrelated domains.
- Portal login and Cardputer bearer tokens are separate auth paths.
- Firmware certificate pins are client-side and must not require shared Caddy or auth changes.
- Before a mutation, record health for SupaChat and at least one unrelated existing application route.
- Authentication completion is a client-to-session boundary, not a password-database boundary. Password verification proves only that Authentik has the intended credential.
- End-to-end Android authentication includes the installed APK, external Authentik interaction, Android deep-link return, OAuth code exchange, native session issuance, mapped SupaChat identity, and expected rooms.
- “Ready to test” requires preflight evidence for every independently exercisable boundary. A phone-only acceptance check may remain, but known gaps, silent branches, infinite waits, and unexercised server exchanges must be resolved first.
