# SupaChat release lessons

- I built Android artifacts from `duels-messaging` after the user required duels and nice-to-have features to remain on branches. The current checkout is not release authorization.
- I treated “generate the APK” as permission to include everything recently implemented. Artifact creation authorizes a target, not an unstated feature scope.
- I exposed emoji reactions while their server endpoint was undeployed. Never ship a control that cannot succeed against the target environment.
- I loaded messages, presence, and an optional duel endpoint in one fatal `Promise.all`. The duel 404 discarded successful K-BUDS history and made a populated room look empty. Core messaging must survive optional-feature failure.
- I described branch ancestry as the feature diff before promoting already-approved group work to `main`. Compare directly with `origin/main` after verifying what main is supposed to contain.
- I left unrelated working-tree changes unresolved and later risked mixing repositories. Inventory every workspace repository, preserve unrelated work, and keep release commits surface-specific.
- EAS “preview” is still distribution. A downloadable APK can reach a user even when it is not in an app store.
- Cancel a wrong asynchronous build immediately, record the cancellation, and rebuild from a clean worktree at the authorized commit.
- Never migrate or delete production chat history based only on an empty client view. Query room counts first; the K-BUDS database contained messages and the client discarded them locally.

