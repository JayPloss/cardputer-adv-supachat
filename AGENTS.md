# SupaChat workspace guardrails

## Authentication claims

- Never describe a login issue as fixed, functional, working, or expected to work based only on account existence, password assignment, password-hash verification, server health, route health, unit tests, or a simulated identity header.
- Authentication is proven only when the same released client and login route used by the person completes the entire flow: credential entry, Authentik authentication, redirect or deep link, token/code exchange, SupaChat session creation, identity mapping, and retrieval of the expected rooms.
- Report partial evidence precisely. For example, `the stored password matches Authentik` is not `Android login works`.
- If the physical Android client has not completed the flow, state `Android login is unproven`. Do not predict success.
- A claim of successful end-to-end login must identify the tested client/build, username, resulting SupaChat identity, and returned room set. Never print or log passwords.

## Authentication test-readiness gate

- For Android authentication work and every APK release, load and follow `skills/gate-android-authentication/SKILL.md`; its checklist is a blocking release gate.

- Do not hand an APK to the user or ask the user to test login while any independently testable stage is merely assumed, partially configured, or known to be unverified. “Ready to test” means engineering preflight is complete, not “ready to discover the next integration failure.”
- Before user acceptance, exercise the whole authentication chain with the release configuration: authorization request, credential acceptance, registered Android callback, callback delivery to the installed package, preservation of PKCE state and verifier across backgrounding or process recreation, token exchange, `/api/native/session`, secure session persistence, identity mapping, and expected-room retrieval.
- Inspect the built APK rather than trusting source configuration. Confirm package name, intent filters, callback URI, embedded issuer/client configuration, and build provenance.
- Test failure paths and time bounds. No authentication or startup operation may leave an indefinite spinner; it must resolve or show an actionable error within a defined timeout.
- Automate or emulate every stage that does not intrinsically require the physical phone. Use the physical-phone run only as the last acceptance gate, with runtime logging attached when possible.
- If any stage cannot be exercised before handoff, identify that exact gap before asking for a test. Do not collapse several untested stages into “please try it.”
