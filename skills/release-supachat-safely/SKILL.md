---
name: release-supachat-safely
description: Gate SupaChat releases by source branch, approved feature scope, and deployed protocol compatibility. Use before building or distributing an Android APK, flashing Cardputer firmware, deploying the SupaChat web app/server, promoting a feature branch, or describing any SupaChat artifact as a release.
---

# Release SupaChat Safely

Treat the Git branch as a release boundary, not an organizational hint.

## Non-negotiable gates

1. Translate authorization into a release manifest containing surface, source ref, commit, approved features, excluded features, target, and rollback.
2. Default every release to `origin/main`. A request to implement, stage, test, commit, push, or build a “new APK” does not authorize releasing a feature branch.
3. Use a non-main source only when the user explicitly names that branch or explicitly authorizes its feature set for that target. Never infer authorization from the current checkout, active goal, recent work, or an existing EAS build.
4. Run `scripts/check-release-source.ps1` in the exact source worktree. Stop on a dirty tree, unexpected ref, or commit mismatch.
5. Compare `git diff --name-status origin/main...HEAD` and `git log origin/main..HEAD` with the manifest. Stop if the artifact would include any excluded feature.
6. Build from a clean worktree at the recorded commit. Never release from a convenient dirty or feature worktree.
7. Verify client/server compatibility before distribution. A visible control must have a deployed backing endpoint. Required chat/history loading must not share a fatal `Promise.all` with optional staged features.
8. Treat EAS preview uploads, APK links, physical flashes, and server deployments as distribution. “Internal” does not relax branch authorization.
9. Put the source branch, commit, included features, and explicit exclusions in the build/deployment comment.
10. Do not wait for asynchronous EAS completion unless asked. Return the build URL and source manifest immediately.

## Surface rules

### Android APK

- Build from Android `origin/main` unless feature-branch distribution is explicit.
- Confirm the APK contains no UI or API calls from excluded branches.
- Test room switching and historical loading against the currently deployed server.
- Make optional endpoints independently failure-tolerant.
- Increment the semantic app version for a corrected artifact; let EAS manage `versionCode`.

### Cardputer

- Run `$emulate-cardputer-contracts`, `$verify-cardputer-ui` when UI changes, then `$flash-cardputer-safely`.
- Resolve physical device identity after selecting the authorized source commit.
- Do not flash feature-branch firmware merely because it compiles.

### Web and server

- Run `$protect-shared-hetzner-routes` before live inspection or mutation.
- A push to `main` is not a deployment request; a request to deploy `main` is not authorization to deploy feature branches.
- Snapshot production health and data counts before mutation, deploy only the manifest commit, then verify representative rooms, auth, invites, and an unrelated shared-host route.

Read [references/lessons-learned.md](references/lessons-learned.md) when a release follows feature work or repairs a bad artifact.

