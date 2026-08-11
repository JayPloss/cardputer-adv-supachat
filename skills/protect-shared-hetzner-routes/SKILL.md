---
name: protect-shared-hetzner-routes
description: Protect existing applications, Caddy routes, TLS configuration, and authentication on a shared Hetzner server. Use before deploying SupaChat or another app to le954.ca, editing Caddy, changing login or bearer authentication, rotating certificates, adding paths, restarting shared services, or diagnosing server connectivity.
---

# Protect Shared Hetzner Routes

Assume the host serves many unrelated production applications.

1. Inventory the current route, service, container, and auth boundaries read-only.
2. Scope SupaChat changes under `/supachat` only.
3. Preserve all existing routes, middleware, cookies, login flows, headers, certificates, and redirects.
4. Do not replace shared Caddy configuration wholesale.
5. Do not weaken or bypass unrelated authentication.
6. Keep Cardputer bearer authentication separate from portal login authentication.
7. Validate existing representative routes before and after deployment.
8. Roll back only SupaChat-owned changes if validation fails.
9. Treat certificate rotation as shared infrastructure; prefer client trust updates that do not change server behavior.

Read [references/boundaries.md](references/boundaries.md) before any server mutation.
