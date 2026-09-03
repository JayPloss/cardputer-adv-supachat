---
name: protect-shared-hetzner-routes
description: Protect existing applications, Caddy routes, TLS configuration, and authentication on a shared Hetzner server. Use before deploying SupaChat, editing Caddy, changing login or bearer authentication, rotating certificates, adding paths, restarting shared services, or diagnosing server connectivity.
---

# Protect Shared Hetzner Routes

Assume the host serves many unrelated production applications.

1. Inventory the current route, service, container, and auth boundaries read-only.
2. Scope SupaChat changes to its dedicated virtual hosts and service only.
3. Preserve all existing routes, middleware, cookies, login flows, headers, certificates, and redirects.
4. Do not replace shared Caddy configuration wholesale.
5. Do not weaken or bypass unrelated authentication.
6. Keep Cardputer bearer authentication separate from portal login authentication.
7. Validate existing representative routes before and after deployment.
8. Roll back only SupaChat-owned changes if validation fails.
9. Treat certificate rotation as shared infrastructure; prefer client trust updates that do not change server behavior.

## Authentication completion gate

Account creation, password assignment, `check_password`, healthy routes, and server-side identity simulation are intermediate checks only. Never call login fixed or functional, and never predict that it will work, until the actual released client completes credential entry, Authentik authentication, its redirect or deep link, code/token exchange, SupaChat session creation, identity mapping, and expected-room retrieval.

When the real client has not completed that sequence, report the narrow facts established and explicitly label end-to-end login unproven. A successful claim must record the client/build, username, resulting SupaChat identity, and returned room set without exposing credentials.

Do not use the user as the first integration test. Before handing over a build, validate every independently testable stage across the release-configured client and live service, including built-APK callback registration, callback resumption, PKCE state/verifier survival, token exchange, native-session exchange, identity mapping, room retrieval, and bounded error handling. The user's physical-device run is final acceptance after this preflight, not a way to discover omitted integration work.

Read [references/boundaries.md](references/boundaries.md) before any server mutation.
