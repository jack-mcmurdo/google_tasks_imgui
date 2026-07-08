# Plan: PKCE-hardened OAuth + rotate/purge leaked client_secret from history + CI zero-config packaging

**Goal:** Add PKCE (RFC 7636) as defense-in-depth to the Tasks OAuth flow, restore sourcing `client_secret` as a real credential (env var / gitignored file, never hardcoded or committed), rotate the leaked secret value, purge the old one from git history with a force-push, **and make `release.yml` actually inject the secret at build time so downloaded packages get zero-config login** (currently missing — a gap discovered while planning, not optional polish).

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
- `CMakeLists.txt` — keep the `if(EXISTS client_secret.json) install(...) else() message(WARNING ...) endif()` bundling block as-is.
- `.gitignore` — keep the `client_secret.json` line.
- `README.md` — "OAuth Credentials" section explains: `client_id` hardcoded (non-sensitive), `client_secret` sourced via env var/file (real credential, PKCE doesn't remove the requirement), same bundling story as before for built packages via CI-injected secret.
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
- [ ] Commit and push the `release.yml` + `main.cpp` changes (non-destructive — no secret values are in the diff).
- [ ] Delete the old, now-leaked-and-abandoned OAuth client in Cloud Console once the new one is confirmed working end-to-end (next step) — no reason to leave a dead-but-still-registered client around.
- [ ] **Re-cut the `v2.0.0` release** so it actually ships with a bundled secret (the one already published does not): delete the existing `v2.0.0` tag/release (`gh release delete v2.0.0`, `git tag -d v2.0.0 && git push origin :refs/tags/v2.0.0`) and re-tag/push once the workflow fix is in, OR cut a new patch tag (e.g. `v2.0.1`) if preserving the existing release's history is preferred — ask the user which before doing either, since both mutate published release state.
- [ ] **Verify end-to-end**: download the newly-built `.deb`/`.tgz` from the Actions run or Release page, install/extract it, confirm `/usr/share/google-tasks-imgui/client_secret.json` (or the tarball's equivalent path) exists and contains the expected `client_id`/`client_secret` keys, then actually run the installed binary and confirm the OAuth login flow completes without any env var or manual file placement — this is the real acceptance test for "zero-config," not just a clean CI run.
- [ ] **Separate, destructive phase — confirm with user immediately before running:**
  - [ ] `git filter-repo --replace-text <(printf 'REDACTED==>REDACTED')` to scrub the leaked secret from every commit that contains it.
  - [ ] Re-add the `origin` remote (`git filter-repo` removes it as a safety measure).
  - [ ] Force-push the rewritten branch: `git push origin master --force`.
  - [ ] Force-push all rewritten tags too — `filter-repo` rewrites the commits tags point to, but the *remote* still has the old tag objects: `git push origin --tags --force` (or delete+recreate `v1.0.0`/`v1.1.0`/`v1.2.0`/`v2.0.0`/(re-cut tag) explicitly if a plain `--force` push of tags is rejected).
  - [ ] Verify the secret is gone from remote history: fresh shallow-less clone to a temp dir, `git log --all -p | grep GOCSPX...` should return nothing.

## Edge cases & risks
- **Every commit hash after `2978ddc` changes.** Anyone else with a clone of this repo (forks, other machines) will diverge and need to re-clone or hard-reset to the rewritten history — there is no clean merge path back.
- **`git filter-repo` refuses to run on a repo with an `origin` remote by default** (expects a fresh clone) — pass `--force`, and remember it deletes the `origin` remote afterward as a safety feature; re-add it before pushing.
- **GitHub Releases are keyed by tag name, not commit SHA** — after force-pushing rewritten tags, the existing `v2.0.0` release entry and its uploaded artifacts should remain associated correctly, but this hasn't been verified empirically; check the Releases page after pushing.
- **The leaked secret is now rotated/dead**, so purging it from history is pure hygiene, not a live risk mitigation — worth doing, but not urgent.
- **Ordering matters for the release-fix step**: do the `release.yml` fix and re-cut *before* the history-purge force-push, not after — re-cutting a tag is itself a force-push-like operation (moving an existing tag ref), so it's cleaner to land all tag-mutating work in one pass rather than force-pushing tags twice.
- **The already-published `v2.0.0` release currently has broken zero-config login** (no bundled secret) — anyone who already downloaded it got a build requiring manual `GOOGLE_CLIENT_SECRET`/`client_secret.json` setup. Re-cutting fixes it going forward but doesn't un-ship the already-downloaded artifacts.
- **Both `CLIENT_ID` and `CLIENT_SECRET` must exist in the `Deploy` environment and the job must declare `environment: Deploy`** — without the job-level `environment:` key, `secrets.CLIENT_ID`/`secrets.CLIENT_SECRET` silently resolve empty (GitHub Actions renders an unset/inaccessible secret as an empty string, not an error) even if the environment secrets genuinely exist, because environment secrets aren't visible to a job that hasn't opted into that environment. `load_client_secret_file` would then correctly reject the resulting empty-field JSON as invalid, so the failure mode is "falls back to no bundled secret" rather than shipping a broken one — but a green CI run alone doesn't prove the secrets were actually injected; the end-to-end verify step (below) is what actually proves it.
- **GitHub push protection will flag a hardcoded `client_id` even though it isn't sensitive** — already hit once (see Steps). Worth remembering if `client_id` is ever reintroduced into source for any reason (e.g. a future refactor) rather than assuming the earlier "it's fine to hardcode, Google doesn't treat it as confidential" reasoning is the end of the story — GitHub's scanner doesn't care what Google's stance is.
- **`environment:` on a job creates a visible "Deployment" entry** in the repo's Environments/Deployments UI for every release build — cosmetic, but worth knowing so it's not mistaken for something unexpected.
