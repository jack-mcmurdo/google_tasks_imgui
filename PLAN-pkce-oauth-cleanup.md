# Plan: PKCE-hardened OAuth + full history/release purge + CI zero-config packaging

**Goal:** Add PKCE (RFC 7636) as defense-in-depth to the Tasks OAuth flow, restore sourcing `client_secret`/`client_id` as real credentials (env var / gitignored file / GitHub environment secrets — never hardcoded or committed), make `release.yml` actually inject them at build time so downloaded packages get zero-config login (currently missing — a gap discovered while planning, not optional polish), then **finish with a one-time clean sweep**: delete all four existing GitHub Releases/tags, purge the leaked secret from git history, force-push, and cut a single fresh `v1.0.0` release on the clean result.

## Approach — corrected after empirical testing
**Original premise was wrong and has been retracted.** Google's docs list `client_secret` as "Optional" in the native-app token-exchange parameter table, which read as "PKCE lets you drop it." Empirical test against the live token endpoint (plain `curl`, no `client_secret`, valid `client_id` + `refresh_token`) returned `400 invalid_request: "client_secret is missing."` — Google's server requires it for this registered Desktop client regardless of PKCE. Separately, Google's general auth guidance (`developers.google.com/workspace/guides/auth-overview`) explicitly says: *"You should never include your unencrypted client secret in your app... store the client secret securely"* — contradicting the "safe to commit, Desktop secrets aren't confidential" framing this repo's README carried before tonight.

Corrected, verified approach:
- `client_secret` is a real credential — sourced via `GOOGLE_CLIENT_SECRET` env var or gitignored `client_secret.json` (the original design from commit `9dd87ca`, which was correct all along), never hardcoded or committed. Still bundleable into *built packages* (not source control) via CI secret injection, matching RFC 8252's accepted reality that native-app secrets can't be kept truly confidential once shipped, without contradicting "never commit it to git."
- `client_id`, while not actually confidential (visible in every login's browser URL), is *also* kept out of source entirely — not for confidentiality, but because GitHub's push-protection secret scanner flags the "Google OAuth Client ID" pattern regardless of Google's own stance, and there's no upside to fighting that scanner over a value with zero benefit to hardcoding. Sourced identically to `client_secret`: `GOOGLE_CLIENT_ID` env var or `client_secret.json`.
- PKCE (`code_verifier`/`code_challenge`, S256) is added anyway, alongside `client_secret` — it's small (~30 lines, OpenSSL already linked), harmless, and adds a real, independent protection against local authorization-code interception.

Decisions locked in (per user):
- Rotate the leaked secret in Cloud Console — done. User actually created a brand-new OAuth client rather than resetting the secret on the old one, so `client_id` also changed (the old, now-abandoned client should eventually be deleted in Cloud Console once the new one is confirmed working).
- Purge the old leaked `client_secret` value from git history and force-push — confirm again immediately before the actual force-push.
- End-to-end zero-config login for anyone downloading a release package is a hard requirement, not a nice-to-have — `release.yml` must inject both values before `cpack` runs, using GitHub Actions **environment** secrets, not plain repo secrets or anything hardcoded: user created a GitHub Environment named `Deploy` with secrets `CLIENT_ID` and `CLIENT_SECRET` in it (not Google Cloud Secret Manager — that needs a GCP service account just to fetch two values in CI, no benefit at this scale). Verified via `gh api repos/.../environments/Deploy`: `protection_rules: []`, `deployment_branch_policy: null` — no required reviewers, no wait timer, no branch/tag restriction, so the tag-triggered workflow can access it without any extra gating. Jobs must declare `environment: Deploy` to read an environment-scoped secret — a repo-level `secrets.X` reference doesn't work for environment secrets without that.
- **Neither `CLIENT_ID` nor `CLIENT_SECRET` literal values appear anywhere in this repo** (source, workflow YAML, or this plan doc) — first attempt at committing hardcoded `client_id` tripped GitHub push protection (`Google OAuth Client ID` pattern match) even though it isn't truly sensitive; rather than click through the "allow secret" override, both values were moved to the `Deploy` environment and the local commit that contained the literal was un-committed (`git reset --soft`, safe since it had never been pushed) and redone.

## Changes
- `src/auth.h` — keep `set_client_credentials(client_id, client_secret)` (both params; not renamed).
- `src/auth.cpp`:
  - Keep `g_client_secret`, restore it in both `start_oauth_flow`'s token-exchange `Params` and `refresh_token_sync`'s `Params`.
  - Add PKCE helpers alongside it (small, local, not shared with `keep_auth.cpp`): `generate_code_verifier()` (32 random bytes via `RAND_bytes`, base64url-encoded → 43 chars) and `code_challenge_s256(verifier)` (SHA-256 via `EVP_Digest`, base64url-encoded). OpenSSL is already linked.
  - `start_oauth_flow`: generate `code_verifier` once per call; append `&code_challenge=...&code_challenge_method=S256` to `auth_request_url`; add `{"code_verifier", code_verifier}` to the token-exchange `Params` *alongside* (not instead of) `{"client_secret", g_client_secret}`.
  - Both entry points (`start_oauth_flow`'s early-out, `refresh_token_sync`'s guard) should check `g_client_secret` is non-empty too, not just `g_client_id`.
- `src/main.cpp`:
  - Keep `load_client_secret_file()` and the `client_secret.json`/`GOOGLE_CLIENT_ID`/`GOOGLE_CLIENT_SECRET`/four-path-search block exactly as it was (from commit `9dd87ca`) — no hardcoded default for either `client_id` or `client_secret`. If neither is supplied, the app warns and Tasks login won't work, same as before tonight's changes.
- `CMakeLists.txt`:
  - Keep the `if(EXISTS client_secret.json) install(...) else() message(WARNING ...) endif()` bundling block as-is.
  - Bump `project(GoogleTasksImGui VERSION ...)` from `2.0.0` down to `1.0.0` — user chose to restart release numbering at `v1.0.0` on the clean, rewritten history. Keeps `CPACK_PACKAGE_VERSION` (derived from `PROJECT_VERSION`) consistent with the git tag rather than claiming `2.0.0` while tagged `v1.0.0`.
- `.gitignore` — keep the `client_secret.json` line.
- `README.md` — **done.** "OAuth Credentials" section now explains: both `client_id` and `client_secret` sourced via env var/file (only `client_secret` is a genuine credential; `client_id` is kept out of source purely to avoid fighting GitHub's secret scanner, not for confidentiality), PKCE doesn't remove the `client_secret` requirement, CI injects both from the `Deploy` environment's `CLIENT_ID`/`CLIENT_SECRET` before packaging.
- `.github/workflows/release.yml` — **done.** Had no step that created `client_secret.json` at all, so every package built by this workflow before this fix (including the already-published `v2.0.0` release) shipped with no bundled secret and hits "No OAuth client secret found" on first run. Fixed by adding, to both the `build-ubuntu` and `build-centos` jobs:
  - `environment: Deploy` at the job level (required to read the `Deploy` environment's secrets).
  - A step between `Checkout Code` and `Build and Package`:
    ```yaml
    - name: Write OAuth client secret
      run: echo '{"installed":{"client_id":"${{ secrets.CLIENT_ID }}","client_secret":"${{ secrets.CLIENT_SECRET }}"}}' > client_secret.json
    ```
  Written at the job's working directory root (where `actions/checkout` puts the repo, and where `CMakeLists.txt`'s `EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/client_secret.json"` check looks) — `mkdir build && cd build && cmake ..` runs afterward and finds it. The file exists only transiently on the ephemeral CI runner disk; it's never committed, never uploaded as its own artifact (only `*.deb`/`*.tar.gz`/`*.rpm` are), and is gone once the job finishes. Uses the nested `{"installed": {...}}` shape (matches Google's real downloaded format, and what `load_client_secret_file` expects/what the user's local file actually contains) rather than a flat object. Both `${{ secrets.CLIENT_ID }}` and `${{ secrets.CLIENT_SECRET }}` are resolved by the Actions runner at execution time — the literal values never appear in the YAML source itself.

Explicitly unaffected: `src/keep_auth.{h,cpp}` (separate, genuinely-confidential service-account credential) and everything under `src/keep_api.*`.

## Steps
- [x] Implement PKCE in `src/auth.{h,cpp}`, keeping `client_secret` in both requests.
- [x] Restore `src/main.cpp`'s file/env-var credential sourcing for `client_secret`.
- [x] Restore `CMakeLists.txt` bundling rule and `.gitignore` entry for `client_secret.json`.
- [x] Rebuild and confirm clean compile, no warnings.
- [x] Update `README.md`.
- [x] Commit and push these code changes normally (non-destructive, regular commit/push to `master`) — done in `5b278f3`.
- [x] User rotated the leaked `client_secret` — created a new OAuth client in Cloud Console (new `client_id` + `client_secret`), rather than resetting the secret on the old `client_id`.
- [x] User created a GitHub Environment named `Deploy` and added `CLIENT_SECRET` as an environment secret (confirmed via `gh api`: unrestricted, no protection rules).
- [x] First attempt hardcoded the new `client_id` in `src/main.cpp` and the workflow YAML — **reverted**: GitHub push protection flagged the literal `client_id` as a "Google OAuth Client ID" secret pattern before the commit could even be pushed.
- [x] User added `CLIENT_ID` as a second secret in the same `Deploy` environment. Un-committed the blocked local commit (`git reset --soft`, safe — never pushed) and redid it: `src/main.cpp`'s `client_id` now starts empty (sourced only via `GOOGLE_CLIENT_ID` env var or `client_secret.json`, same as `client_secret` always was); `release.yml`'s injection step now reads both `secrets.CLIENT_ID` and `secrets.CLIENT_SECRET`. No literal credential value of any kind remains in source, workflow, or this plan doc.
- [x] Rebuilt clean; validated `release.yml` YAML.
- [x] Bump `CMakeLists.txt`'s `PROJECT_VERSION` from `2.0.0` to `1.0.0`; rebuilt clean.
- [ ] Commit and push the `release.yml` + `main.cpp` + `CMakeLists.txt` changes (non-destructive — no secret values are in the diff).
- [ ] **Pre-purge verification gate — push a throwaway test tag *before* deleting anything real**, so a broken `release.yml` is caught while the old releases/history are still there as a fallback, not after they're already gone:
  1. `git tag v0.0.1-test && git push origin v0.0.1-test` (matches the `v*.*.*` trigger pattern without colliding with any real version).
  2. Watch the Actions run succeed for both `build-ubuntu` and `build-centos`.
  3. Download the built `.deb` or `.tgz` from the run's artifacts (or the auto-created `v0.0.1-test` release), install/extract it, inspect the bundled `client_secret.json` for real (non-empty) `client_id`/`client_secret` values, then actually run the installed binary and confirm OAuth login completes with zero manual env var/file setup.
  4. Clean up regardless of outcome: `gh release delete v0.0.1-test --yes --cleanup-tag` (or `git push origin :refs/tags/v0.0.1-test` if no release was auto-created).
  5. If anything failed, stop here and fix `release.yml`/the environment secrets before proceeding — do not touch the real releases or history until this gate passes.
- [ ] Delete the old, now-leaked-and-abandoned OAuth client in Cloud Console once the test tag above confirms the new one works — no reason to leave a dead-but-still-registered client around.
- [ ] **Final step — confirm with user immediately before running any of this, it's all destructive/irreversible:** purge history *and* all previous releases in one pass, per user's "keep it clean" instruction:
  1. Delete all four existing GitHub Releases and their tags, both remote and local — `v1.0.0`, `v1.1.0`, `v1.2.0`, `v2.0.0` (the last of which never had working zero-config anyway, so nothing valid is being lost):
     ```
     for t in v1.0.0 v1.1.0 v1.2.0 v2.0.0; do gh release delete "$t" --yes --cleanup-tag; done
     git tag -d v1.0.0 v1.1.0 v1.2.0 v2.0.0   # in case --cleanup-tag didn't also remove locally
     ```
  2. `git filter-repo --replace-text <(printf 'REDACTED==>REDACTED') --force` to scrub the leaked secret from every commit that contains it. With no tags left (step 1), there's nothing else for it to rewrite besides the branch — simpler than the original plan of force-pushing rewritten tags to match old ones.
  3. Re-add the `origin` remote (`git filter-repo` removes it as a safety measure).
  4. Force-push the rewritten branch: `git push origin master --force`.
  5. Verify the secret is gone from remote history: fresh clone to a temp dir, `git log --all -p | grep GOCSPX...` should return nothing, and confirm the GitHub Releases page shows zero releases.
  6. Cut exactly **one** fresh tag on the clean, rewritten history: **`v1.0.0`** (user's choice — restarting numbering now that history is clean, rather than continuing at `v2.0.0`). `CMakeLists.txt`'s `PROJECT_VERSION` was already bumped to match, above.
  7. **Verify end-to-end on the real `v1.0.0` release** the same way as the throwaway test tag above — this is the real acceptance test, not just a green CI run, and shouldn't be skipped just because the test tag already passed (the environment/secrets access could theoretically differ, e.g. if `Deploy`'s policy is ever scoped to specific tag patterns later).

## Edge cases & risks
- **This is now a "delete everything published, then re-publish once" plan, not an incremental fix** — per user's explicit "purge previous releases as well" instruction. All four existing Releases and tags (`v1.0.0`-`v2.0.0`) get deleted before the history rewrite, and exactly one fresh tag is cut afterward. Anyone who already downloaded any of the four old release artifacts keeps their local copy (deleting a GitHub Release doesn't reach into anyone's downloads folder), but the download links and release notes disappear.
- **Every commit hash changes**, not just those after `2978ddc` — deleting all tags first means `git filter-repo` has nothing else to preserve/rewrite, so this is a full-history rewrite in effect. Anyone else with a clone of this repo (forks, other machines) will diverge and need to re-clone or hard-reset to the rewritten history — there is no clean merge path back.
- **`git filter-repo` refuses to run on a repo with an `origin` remote by default** (expects a fresh clone) — pass `--force`, and remember it deletes the `origin` remote afterward as a safety feature; re-add it before pushing.
- **The leaked secret is now rotated/dead**, so purging it from history is pure hygiene, not a live risk mitigation — worth doing, but not urgent. The release/tag purge is the same: hygiene, not urgency.
- **`gh release delete --cleanup-tag` should remove both the Release and its git tag in one call**, but the plan double-checks with `git tag -d` afterward in case the local tag lingers (e.g. if the flag behaves differently than expected) — cheap insurance, `git tag -d` on an already-gone tag just errors harmlessly.
- **The `v0.0.1-test` throwaway tag matches the same `v*.*.*` trigger as real releases** — if the pre-purge verification gate is ever skipped or its cleanup step forgotten, a stray test release could linger publicly. The plan's step 4 (delete `v0.0.1-test`) isn't optional cleanup, it's required regardless of whether the gate passed or failed.
- **Both `CLIENT_ID` and `CLIENT_SECRET` must exist in the `Deploy` environment and the job must declare `environment: Deploy`** — without the job-level `environment:` key, `secrets.CLIENT_ID`/`secrets.CLIENT_SECRET` silently resolve empty (GitHub Actions renders an unset/inaccessible secret as an empty string, not an error) even if the environment secrets genuinely exist, because environment secrets aren't visible to a job that hasn't opted into that environment. `load_client_secret_file` would then correctly reject the resulting empty-field JSON as invalid, so the failure mode is "falls back to no bundled secret" rather than shipping a broken one — but a green CI run alone doesn't prove the secrets were actually injected; the end-to-end verify step (below) is what actually proves it.
- **GitHub push protection will flag a hardcoded `client_id` even though it isn't sensitive** — already hit once (see Steps). Worth remembering if `client_id` is ever reintroduced into source for any reason (e.g. a future refactor) rather than assuming the earlier "it's fine to hardcode, Google doesn't treat it as confidential" reasoning is the end of the story — GitHub's scanner doesn't care what Google's stance is.
- **`environment:` on a job creates a visible "Deployment" entry** in the repo's Environments/Deployments UI for every release build — cosmetic, but worth knowing so it's not mistaken for something unexpected.
