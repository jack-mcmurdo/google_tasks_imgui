# Plan: PKCE-hardened OAuth + rotate/purge leaked client_secret from history

**Goal:** Add PKCE (RFC 7636) as defense-in-depth to the Tasks OAuth flow, restore sourcing `client_secret` as a real credential (env var / gitignored file, never hardcoded or committed), rotate the leaked secret value, and purge the old one from git history with a force-push.

## Approach — corrected after empirical testing
**Original premise was wrong and has been retracted.** Google's docs list `client_secret` as "Optional" in the native-app token-exchange parameter table, which read as "PKCE lets you drop it." Empirical test against the live token endpoint (plain `curl`, no `client_secret`, valid `client_id` + `refresh_token`) returned `400 invalid_request: "client_secret is missing."` — Google's server requires it for this registered Desktop client regardless of PKCE. Separately, Google's general auth guidance (`developers.google.com/workspace/guides/auth-overview`) explicitly says: *"You should never include your unencrypted client secret in your app... store the client secret securely"* — contradicting the "safe to commit, Desktop secrets aren't confidential" framing this repo's README carried before tonight.

Corrected, verified approach:
- `client_id` is genuinely non-sensitive (visible in every login's browser URL) — stays hardcoded in source, `GOOGLE_CLIENT_ID` overridable.
- `client_secret` is a real credential — sourced via `GOOGLE_CLIENT_SECRET` env var or gitignored `client_secret.json` (the original design from commit `9dd87ca`, which was correct all along), never hardcoded or committed. Still bundleable into *built packages* (not source control) via CI secret injection, matching RFC 8252's accepted reality that native-app secrets can't be kept truly confidential once shipped, without contradicting "never commit it to git."
- PKCE (`code_verifier`/`code_challenge`, S256) is added anyway, alongside `client_secret` — it's small (~30 lines, OpenSSL already linked), harmless, and adds a real, independent protection against local authorization-code interception.

Decisions locked in (per user):
- Rotate the leaked secret in Cloud Console (reset secret on the existing `client_id` — no need to also rotate `client_id`, which was never sensitive).
- Purge the old leaked `client_secret` value from git history and force-push — confirm again immediately before the actual force-push.

## Changes
- `src/auth.h` — keep `set_client_credentials(client_id, client_secret)` (both params; not renamed).
- `src/auth.cpp`:
  - Keep `g_client_secret`, restore it in both `start_oauth_flow`'s token-exchange `Params` and `refresh_token_sync`'s `Params`.
  - Add PKCE helpers alongside it (small, local, not shared with `keep_auth.cpp`): `generate_code_verifier()` (32 random bytes via `RAND_bytes`, base64url-encoded → 43 chars) and `code_challenge_s256(verifier)` (SHA-256 via `EVP_Digest`, base64url-encoded). OpenSSL is already linked.
  - `start_oauth_flow`: generate `code_verifier` once per call; append `&code_challenge=...&code_challenge_method=S256` to `auth_request_url`; add `{"code_verifier", code_verifier}` to the token-exchange `Params` *alongside* (not instead of) `{"client_secret", g_client_secret}`.
  - Both entry points (`start_oauth_flow`'s early-out, `refresh_token_sync`'s guard) should check `g_client_secret` is non-empty too, not just `g_client_id`.
- `src/main.cpp`:
  - Keep `load_client_secret_file()` and the `client_secret.json`/`GOOGLE_CLIENT_SECRET`/four-path-search block — this was already correct (from commit `9dd87ca`).
  - Only change: give `client_id` a hardcoded fallback default (`492917157691-lap1e9nte0gvq44t2712o9pmgvfvkofb.apps.googleusercontent.com`), still overridable by `GOOGLE_CLIENT_ID` env var or `client_secret.json`'s embedded `client_id`. `client_secret` has no hardcoded default — must come from env var or file, or the app warns and Tasks login won't work.
- `CMakeLists.txt` — keep the `if(EXISTS client_secret.json) install(...) else() message(WARNING ...) endif()` bundling block as-is.
- `.gitignore` — keep the `client_secret.json` line.
- `README.md` — "OAuth Credentials" section explains: `client_id` hardcoded (non-sensitive), `client_secret` sourced via env var/file (real credential, PKCE doesn't remove the requirement), same bundling story as before for built packages via CI-injected secret.

Explicitly unaffected: `src/keep_auth.{h,cpp}` (separate, genuinely-confidential service-account credential) and everything under `src/keep_api.*`.

## Steps
- [x] Implement PKCE in `src/auth.{h,cpp}`, keeping `client_secret` in both requests.
- [x] Restore `src/main.cpp`'s file/env-var credential sourcing; hardcode only `client_id`.
- [x] Restore `CMakeLists.txt` bundling rule and `.gitignore` entry for `client_secret.json`.
- [x] Rebuild and confirm clean compile, no warnings.
- [x] Update `README.md`.
- [ ] Commit and push these code changes normally (non-destructive, regular commit/push to `master`).
- [ ] User rotates the leaked `client_secret` in Cloud Console (reset secret on the existing `client_id`) and supplies it locally via env var/`client_secret.json` — not something the agent needs the value for, since it's never hardcoded/committed.
- [ ] **Separate, destructive phase — confirm with user immediately before running:**
  - [ ] `git filter-repo --replace-text <(printf 'REDACTED==>REDACTED')` to scrub the leaked secret from every commit that contains it.
  - [ ] Re-add the `origin` remote (`git filter-repo` removes it as a safety measure).
  - [ ] Force-push the rewritten branch: `git push origin master --force`.
  - [ ] Force-push all rewritten tags too — `filter-repo` rewrites the commits tags point to, but the *remote* still has the old tag objects: `git push origin --tags --force` (or delete+recreate `v1.0.0`/`v1.1.0`/`v1.2.0`/`v2.0.0` explicitly if a plain `--force` push of tags is rejected).
  - [ ] Verify the secret is gone from remote history: fresh shallow-less clone to a temp dir, `git log --all -p | grep GOCSPX...` should return nothing.

## Edge cases & risks
- **Every commit hash after `2978ddc` changes.** Anyone else with a clone of this repo (forks, other machines) will diverge and need to re-clone or hard-reset to the rewritten history — there is no clean merge path back.
- **`git filter-repo` refuses to run on a repo with an `origin` remote by default** (expects a fresh clone) — pass `--force`, and remember it deletes the `origin` remote afterward as a safety feature; re-add it before pushing.
- **GitHub Releases are keyed by tag name, not commit SHA** — after force-pushing rewritten tags, the existing `v2.0.0` release entry and its uploaded artifacts should remain associated correctly, but this hasn't been verified empirically; check the Releases page after pushing.
- **The leaked secret is now rotated/dead**, so purging it from history is pure hygiene, not a live risk mitigation — worth doing, but not urgent.
