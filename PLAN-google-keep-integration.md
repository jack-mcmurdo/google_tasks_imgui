# Plan: Google Keep integration (Workspace-only)

**Goal:** Add Google Keep notes as a second content type in the app, browsable/creatable/deletable from a new "Keep" sidebar section parallel to the existing "Lists" section — available only to Google Workspace accounts whose admin has enabled domain-wide delegation for the Keep API.

## Approach

Google Keep has no public API for personal `@gmail.com` accounts. The only official access path (`keep.googleapis.com`) requires:
- A Google Cloud service account with **domain-wide delegation**, authorized by the Workspace admin in the Admin Console for the `https://www.googleapis.com/auth/keep` scope.
- The app impersonates the already-signed-in user (`sub` = their email) via a JWT-bearer token exchange — a completely different auth mechanism from the interactive OAuth flow used for Tasks (`src/auth.cpp`).

**Verdict: adopt** the official API — it's the only legitimate option and the user has confirmed Workspace-only is acceptable. Two hard constraints this imposes on scope:

1. **No update endpoint.** The Keep API (`notes.create/get/list/delete`) has never supported editing a note's content. "Edit" in this app must be emulated as delete-then-recreate. This is a real UX regression vs. real Keep clients (new note ID each edit, no in-place checklist-item toggling) — call it out in the UI ("Saving creates a new note") rather than hiding it.
2. **No archive/pin/color/labels.** These Keep-app concepts don't exist in the API's data model at all. Out of scope for v1, not a bug to "fix" later — the API simply can't do it.

Auth is architecturally separate from the Tasks OAuth flow: Tasks uses per-account user tokens (`Auth::TokenManager`); Keep uses one domain-wide service-account key that mints short-lived impersonation tokens for whichever account is currently selected. Reuse the currently-signed-in `Account.email` as the impersonation subject — no separate Keep login step for the user.

## Changes

- `src/keep_auth.h` / `src/keep_auth.cpp` (new) — service-account JWT-bearer flow:
  - Load service account JSON (contains `client_email`, `private_key`, `token_uri`).
  - Build and RS256-sign a JWT (`iss`=client_email, `sub`=impersonated user email, `scope`=`https://www.googleapis.com/auth/keep`, `aud`=token_uri, `exp`=now+1h) using OpenSSL `EVP_DigestSign` — OpenSSL is already linked (`CMakeLists.txt:11`, `CPPHTTPLIB_OPENSSL_SUPPORT` in `auth.cpp:2`), no new dependency.
  - POST to `https://oauth2.googleapis.com/token` with `grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=<jwt>`, same `httplib::Client` pattern as `auth.cpp:184`.
  - Cache the resulting access token per (service account, user email) pair with expiry, mirroring `EnsureValidToken` (`main.cpp:125`).
  - Resolve the service account key path in this order (mirrors `client_secret.json` resolution in README, but **never packaged**): `GOOGLE_APPLICATION_CREDENTIALS` env var → `keep_service_account.json` next to the binary → one directory up. No install-prefix fallback (that path *is* bundled into packages today — must not apply here).

- `src/keep_api.h` / `src/keep_api.cpp` (new) — `GoogleKeepAPI` class, modeled on `GoogleTasksAPI` (`src/api.h:55`):
  ```cpp
  struct KeepNoteItem { std::string text; bool checked; };
  struct KeepNote {
      std::string name;       // "notes/{id}", empty until created
      std::string title;
      std::string text;       // used when is_list == false
      std::vector<KeepNoteItem> items; // used when is_list == true
      bool is_list = false;
      std::string create_time, update_time;
      bool trashed = false;
  };
  class GoogleKeepAPI {
  public:
      GoogleKeepAPI(const std::string& access_token);
      std::vector<KeepNote> list_notes(bool show_trashed = false);
      std::optional<KeepNote> create_note(const KeepNote& note);
      bool delete_note(const std::string& name); // confirm trash-vs-permanent semantics against live API docs during implementation
  private:
      std::string access_token_;
  };
  ```
  No `update_note` — intentionally absent (see Approach).

- `src/main.cpp`:
  - `AppState`: add `std::shared_ptr<API::GoogleKeepAPI> keep_api`, `std::vector<API::KeepNote> notes`, `std::string selected_note_id`, `bool keep_available`, `std::string keep_error`.
  - `SelectAccount` (`main.cpp:137`): after selecting the Tasks account, attempt to mint a Keep impersonation token for the same email. On success, populate `keep_api`/`notes`/`keep_available=true`. On 401/403/missing-key, set `keep_available=false` and a short `keep_error` — don't crash, don't retry in a loop.
  - `RenderSidebar` (`main.cpp:180`): add a new `SectionHeaderText("Keep")` block after the existing "Lists" block, same indent/`Selectable` pattern as `main.cpp:242-265`. If `!keep_available`, render it greyed out with `keep_error` as a tooltip (e.g. "Ask your Workspace admin to enable Keep API delegation — see README").
  - New render pane for a selected note (title + text, or title + checklist items), reusing the existing tasks-pane layout conventions. "Save" on an existing note calls `delete_note` then `create_note` and swaps `selected_note_id` to the new name; make this explicit in the UI copy.

- `README.md`:
  - Note in the main OAuth section that Keep is a separate, additional auth path (service-account, not the Desktop OAuth client) and does not work for personal Google accounts.
  - New section **"Enabling Google Keep for Workspace Admins"** (see Steps below for exact content) covering GCP setup, domain-wide delegation, and — critically — a security note that the service-account key must never be committed, packaged, or shared outside IT, since it can impersonate any user in the domain for the authorized scope.

## Steps

- [ ] Confirm exact `notes.delete` semantics (trash vs. permanent) against `https://developers.google.com/keep/api/reference/rest/v1/notes/delete` before wiring the delete button; word the UI confirmation dialog accordingly ("permanently delete" vs "move to trash").
- [ ] Implement `src/keep_auth.{h,cpp}`: JSON key loading, RS256 JWT construction (base64url header/claims + OpenSSL signature), token exchange, in-memory cache with expiry.
- [ ] Implement `src/keep_api.{h,cpp}`: `KeepNote`/`KeepNoteItem` structs, JSON (de)serialization matching the `notes` resource (`title`, `body.text.text` or `body.list.listItems[]`, `trashed`, `createTime`, `updateTime`), `list_notes`/`create_note`/`delete_note`.
- [ ] Add `keep_service_account.json` to `.gitignore` (alongside existing `client_secret.json` entry) and confirm CMake/CPack packaging rules do **not** pick it up from the repo root the way `client_secret.json` is bundled.
- [ ] Wire `AppState` + `SelectAccount` to attempt Keep token acquisition per selected account; store availability/error instead of throwing.
- [ ] Add the "Keep" sidebar section in `RenderSidebar`, disabled state with tooltip when unavailable.
- [ ] Add a notes list/detail pane: view text or checklist notes, create new note (text or checklist), "save" = delete+recreate with a visible warning, delete note with confirmation.
- [ ] Update `README.md`:
  - [ ] Add the "does not work for personal accounts" caveat near the existing OAuth section.
  - [ ] Add the **Workspace Admin** section with these exact steps:
    1. In Google Cloud Console, select (or create) the project linked to your Workspace, and enable the **Google Keep API**.
    2. Create a **Service Account** (IAM & Admin → Service Accounts). Note its **Client ID** (not the email) and download a JSON key — treat this file as a credential, not a config file.
    3. In **Admin Console → Security → Access and data control → API controls → Domain-wide delegation**, add the service account's Client ID with scope `https://www.googleapis.com/auth/keep`. Do not grant broader scopes than needed.
    4. Distribute the JSON key file to each user's machine out-of-band (not via this app's installer/package), either via `GOOGLE_APPLICATION_CREDENTIALS` env var or a `keep_service_account.json` file next to the app binary.
    5. Security notes: this key can act as *any* user in the domain for the Keep scope — restrict file permissions, never commit it, never include it in a distributed package/installer, and rotate/revoke it (delete the domain-wide delegation entry) immediately if it's ever exposed.

## Edge cases & risks

- **No in-place edit or checklist-item toggling** — every content change is a new note (new ID, resets create/update time, drops attachments/sharing on the old note). This is an API limitation, not a bug; surfaced in UI copy per above.
- **Service-account key exposure is high-blast-radius** — unlike the Desktop OAuth client secret (which Google treats as non-confidential), this key is a real credential. The packaging/gitignore step above must be verified, not assumed.
- **Low default API quota** — Keep API quotas are historically tight; heavy note lists/creates may hit rate limits. Consider a quota-increase request if usage is more than light/personal use per Workspace.
- **Silent unavailability for non-Workspace accounts** — must degrade gracefully (greyed-out section + tooltip), since most users adding an account via "Add Account" will still be on personal Gmail for Tasks.
