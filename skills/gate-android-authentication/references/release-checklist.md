# SupaChat Android authentication release checklist

Copy this table into the work record. Every row requires `PASS` plus concrete evidence. `ASSUMED`, `CONFIGURED`, `NOT RUN`, and `SHOULD WORK` are failures.

| Gate | Required evidence | Status |
|---|---|---|
| Approved scope | Requested features and exclusions recorded; artifact contents agree | ☐ |
| Source provenance | Repository, commit, dirty-state review, and release inputs recorded | ☐ |
| Live route safety | SupaChat and representative shared-host routes healthy before mutation | ☐ |
| Authentik provider | Public client, issuer, authorization endpoint, token endpoint, PKCE S256, and exact redirect URI verified live | ☐ |
| Account credential | Intended username exists and the private credential verifies in Authentik without exposing it | ☐ |
| Identity contract | Username maps to the intended stable SupaChat user ID | ☐ |
| Room contract | Expected rooms and room-specific display names are asserted | ☐ |
| Bounded startup | Secure-session loading resolves or visibly errors within the defined timeout | ☐ |
| Bounded login | Browser launch, callback wait, token exchange, and native-session exchange each time out visibly and allow retry | ☐ |
| APK provenance | APK filename, version, commit, SHA-256, build type, and signing identity recorded | ☐ |
| APK package | Built manifest contains the expected Android package/application ID | ☐ |
| APK callback | Built manifest resolves the exact redirect URI to the correct exported activity and launch mode | ☐ |
| Embedded config | Built artifact contains the intended issuer, client ID, API origin, and redirect URI | ☐ |
| Callback warm start | Login returns to an already-running app and yields the original state/code | ☐ |
| Callback cold start | Login returns after activity/process recreation and resumes from persisted pending-auth state | ☐ |
| PKCE continuity | Returned state matches persisted state and the original verifier redeems the code | ☐ |
| Token request | Live logs show the release client POSTing to the token endpoint; response succeeds | ☐ |
| Native exchange | Live logs show the access token exchanged at `/api/native/session`; response succeeds | ☐ |
| Authenticated bootstrap | App retrieves session/profile and expected rooms, then leaves all loading states | ☐ |
| Session persistence | Cold restart restores the SupaChat session or visibly returns to login | ☐ |
| Cancellation | Browser cancel returns control and exposes a retryable non-loading state | ☐ |
| State mismatch | Mismatched/missing state is rejected with a visible error and retry | ☐ |
| Expired/reused code | Exchange failure is visible, bounded, and retryable | ☐ |
| Offline/DNS/TLS failure | Each failure exits loading within the timeout with an actionable message | ☐ |
| Logout/account switch | Local session and pending-auth state clear; the next login cannot inherit the wrong identity | ☐ |
| Automated regression | Unit/contract tests cover state transitions, timeouts, process recreation, identity, and rooms | ☐ |
| Live postflight | SupaChat, Authentik, and representative shared-host routes remain healthy | ☐ |
| Physical acceptance | Installed release APK logs in, maps the expected identity, shows expected rooms, restarts cleanly, and logs out | ☐ |

## Evidence requirements

Record timestamps and sanitized outputs for each live boundary. Correlate the authorization request, callback, token POST, native-session POST, and authenticated bootstrap. Absence of a downstream request is evidence of the boundary where processing stopped, not evidence that earlier configuration is sufficient.

Inspect the final APK even when the generated native project or source configuration looks correct. A prior artifact, wrong branch, stale generated manifest, or changed build input invalidates source-only checks.

For lifecycle testing, cover both warm return and forced activity/process recreation. PKCE state and verifier held only in component memory fail this gate.

The evidence record must finish with one of these exact dispositions:

- `BLOCKED — NOT READY FOR DEVICE ACCEPTANCE`
- `PREFLIGHT PASSED — PHYSICAL-DEVICE ACCEPTANCE REMAINS`
- `END-TO-END ANDROID LOGIN PASSED`

