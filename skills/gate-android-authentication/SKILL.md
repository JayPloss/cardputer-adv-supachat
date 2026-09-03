---
name: gate-android-authentication
description: Prove SupaChat Android authentication is ready before building, distributing, or asking for a device test. Use for login changes, Authentik/OIDC/PKCE changes, callback or loading failures, session handling, and every Android APK release.
---

# Gate Android Authentication

Treat authentication as one stateful client-to-service transaction. Do not validate its components in isolation and infer that the transaction works.

Read [references/release-checklist.md](references/release-checklist.md) for every invocation. Create an evidence record from that checklist and complete every applicable row.

## Non-negotiable gate

Do not describe an APK as ready to test, distribute it, or ask the user to try login until every pre-device row is `PASS`. The physical-device run is final acceptance, not the first integration test.

`Account exists`, `password matches`, `server is healthy`, `callback appears in source`, and `unit tests pass` are partial facts. None substitutes for the complete flow.

When a row cannot pass:

1. Stop the handoff.
2. Identify the exact failed boundary and its evidence.
3. Fix it within the authorized scope.
4. Re-run that row and every downstream row it can affect.

Never leave authentication, startup, callback handling, token exchange, or session loading with an unbounded spinner. Every asynchronous boundary must have a timeout, a visible actionable error, and a safe retry path.

## Required proof sequence

1. Establish release provenance and approved feature scope. Do not infer either from the checked-out branch alone.
2. Verify live Authentik client/provider configuration and the intended account without exposing secrets.
3. Inspect the built APK—not only source—for package identity, callback intent filters, issuer, client ID, and version provenance.
4. Exercise authorization through the release configuration and prove callback delivery to the installed package.
5. Prove PKCE state and verifier survive browser backgrounding and Android activity/process recreation.
6. Observe a real token-endpoint POST and successful code exchange.
7. Observe `/api/native/session`, then verify the resulting SupaChat identity and exact room set.
8. Verify secure persistence, cold restart, logout, retry, cancellation, malformed callback, expired code, offline behavior, and timeout behavior.
9. Run the final physical-device acceptance with runtime logging attached when available.

Use secrets only through private credential storage or protected process input. Never print passwords, authorization codes, access tokens, session tokens, PKCE verifiers, or credential-bearing URLs.

## Completion language

- Before all pre-device rows pass: `Not ready for device acceptance.`
- After pre-device rows pass: `Engineering preflight passed; physical-device acceptance remains.`
- After the installed release APK completes the flow: `Android login passed end to end`, followed by the APK identity, username, mapped identity, room set, and evidence timestamps.

Do not say `fixed`, `functional`, `working`, `ready`, or `should work` when the corresponding gate has not passed.

