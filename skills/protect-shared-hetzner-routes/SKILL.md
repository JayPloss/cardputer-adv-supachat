---
name: protect-shared-hetzner-routes
description: Protect existing applications, Caddy routes, TLS configuration, authentication, and release boundaries on the shared Hetzner server. Use before deploying or diagnosing SupaChat at supachat.net, editing Caddy, changing login or bearer authentication, rotating certificates, adding paths, restarting services, or promoting web/server code.
---

# Protect Shared Hetzner Routes

Assume the host serves many unrelated production applications. Run `$release-supachat-safely` before any deployment.

1. Inventory the current route, service, container, and auth boundaries read-only.
2. Record the authorized source ref and commit. Default to `origin/main`; never deploy the current feature checkout by convenience or inference.
3. Scope SupaChat changes to its dedicated `supachat.net` routes and service only.
4. Preserve all existing routes, middleware, cookies, login flows, headers, certificates, and redirects.
5. Do not replace shared Caddy configuration wholesale.
6. Do not weaken or bypass unrelated authentication.
7. Keep Cardputer bearer authentication separate from portal login authentication.
8. Compare the deployed endpoint set with released web, Android, and Cardputer clients. Do not expose controls for undeployed endpoints.
9. Validate auth, rooms, historical messages, invites, and an unrelated representative route before and after deployment.
10. Roll back only SupaChat-owned changes if validation fails.
11. Treat certificate rotation as shared infrastructure; prefer client trust updates that do not change server behavior.

Read [references/boundaries.md](references/boundaries.md) before any server mutation.
