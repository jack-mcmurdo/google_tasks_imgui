# Google Tasks C++ App

A fast, standalone, and feature-rich C++ desktop application using Dear ImGui to manage Google Tasks. It features seamless system-browser OAuth login, advanced task features (subtasks, drag-and-drop), and automated cross-platform release pipelines.

## Build Instructions

### Ubuntu / Debian (e.g., Ubuntu 24.04)

**Dependencies:**
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libglfw3-dev libssl-dev git
```

**Build & Package:**
```bash
git clone --recursive <your-repo-url>
cd google-notes-app
mkdir build && cd build
cmake ..
make -j$(nproc)
cpack -G DEB # To generate a .deb package
cpack -G TGZ # To generate a standalone tarball
```

### CentOS / RHEL (e.g., CentOS Stream 9)

**Dependencies:**
```bash
sudo dnf install -y epel-release
sudo dnf groupinstall -y "Development Tools"
sudo dnf install -y gcc-c++ cmake glfw-devel openssl-devel rpm-build git
```

**Build & Package:**
```bash
git clone --recursive <your-repo-url>
cd google-notes-app
mkdir build && cd build
cmake ..
make -j$(nproc)
cpack -G RPM # To generate an .rpm package
```

## OAuth Credentials

The app needs a Google OAuth 2.0 **Desktop** client to sign in. Create one in the
[Google Cloud Console](https://console.cloud.google.com/apis/credentials) with the
redirect URI `http://127.0.0.1:8080/`, then download the `client_secret.json`.

At startup the credentials are resolved in this order:

1. `GOOGLE_CLIENT_ID` / `GOOGLE_CLIENT_SECRET` environment variables.
2. `client_secret.json` next to the binary (or one directory up).
3. `client_secret.json` under the install prefix (`/usr/share/google-tasks-imgui/`).

For a shipped package, place `client_secret.json` at the repo root before running
CMake — it is bundled into the `.deb`/`.rpm`/tarball automatically (it is gitignored,
so it never lands in source control). In CI, inject it as a secret before packaging.

> For a Desktop OAuth client, Google does not treat the client secret as confidential,
> so bundling it for distribution is expected. Note the unverified consent screen is
> capped at 100 users until the app passes Google's OAuth verification.

This OAuth flow only covers Google Tasks. Google Keep uses a completely separate,
additional auth path (a Workspace service account, not the Desktop OAuth client above)
and **does not work for personal `@gmail.com` accounts at all** — see the next section.

## Enabling Google Keep for Workspace Admins

Google Keep has no public API for personal accounts. The only official access path
(`keep.googleapis.com`) requires a Google Cloud service account authorized for
**domain-wide delegation** by your Workspace admin. If this isn't set up, the app's
"Keep" sidebar section will simply show as unavailable — Tasks is unaffected.

1. In Google Cloud Console, select (or create) the project linked to your Workspace,
   and enable the **Google Keep API**.
2. Create a **Service Account** (IAM & Admin → Service Accounts). Note its **Client ID**
   (not the email) and download a JSON key — treat this file as a credential, not a
   config file.
3. In **Admin Console → Security → Access and data control → API controls →
   Domain-wide delegation**, add the service account's Client ID with scope
   `https://www.googleapis.com/auth/keep`. Do not grant broader scopes than needed.
4. Distribute the JSON key file to each user's machine out-of-band (not via this app's
   installer/package), either via the `GOOGLE_APPLICATION_CREDENTIALS` environment
   variable or a `keep_service_account.json` file next to the app binary (or one
   directory up).
5. **Security notes:** this key can act as *any* user in the domain for the Keep scope —
   restrict its file permissions, never commit it, never include it in a distributed
   package/installer, and rotate/revoke it (delete the domain-wide delegation entry)
   immediately if it's ever exposed.

Once configured, the app impersonates whichever account is currently signed in for
Tasks — there is no separate Keep login step. Keep's API also has real limitations
worth knowing before you rely on it:

- **No in-place editing.** The Keep API has never supported updating a note's content.
  "Save" on an existing note deletes it and creates a new one (new ID, reset
  create/update time) — the app surfaces this in the UI rather than hiding it.
- **Deletes are permanent.** Unlike Tasks, the Keep API has no trash/undo for
  `notes.delete` — the app asks for confirmation before deleting a note.
- **No archive/pin/color/labels.** These Keep-app concepts don't exist in the API's
  data model, so they're not supported here.

## Acknowledgements

Feature inspiration directly modeled after the robust work done in [google-task-desktop](https://github.com/codad5/google-task-desktop) by author `codad5`.
