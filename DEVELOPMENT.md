# Development Guide

Everything in this file is for people **building the app from source or maintaining
it** — packaging, and configuring the OAuth client the app signs in with. If you just
installed a released `.deb`/`.rpm`, none of this applies to you; see the
[README](README.md).

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

The app signs in with a Google OAuth 2.0 **Desktop** client using Authorization Code +
PKCE (RFC 7636) for extra protection against a local process intercepting the login
redirect. PKCE does **not** remove the need for a `client_secret`, though — Google's
token endpoint still requires one for this client type; confirmed directly against the
live API, not just docs.

Neither `client_id` nor `client_secret` is hardcoded. `client_secret` is a real credential
and was never going to be committed. `client_id` isn't actually confidential (it's visible
in the browser URL on every login), but it's sourced the same way anyway — GitHub's push
protection flags a hardcoded Google OAuth client ID as a secret regardless of Google's own
stance, and there's no upside to fighting that scanner over a value with no benefit to
hardcoding. At startup both are resolved in this order:

1. `GOOGLE_CLIENT_ID` / `GOOGLE_CLIENT_SECRET` environment variables.
2. `client_secret.json` in the working directory the app is launched from (or one
   directory up, which covers running from `build/`) — the standard file Google Cloud
   Console lets you download for a Desktop OAuth client.
3. `client_secret.json` under the install prefix (`/usr/share/google-tasks-imgui/`
   or `/usr/local/share/google-tasks-imgui/`).

For a shipped package, place `client_secret.json` at the repo root before running CMake —
it is bundled into the `.deb`/`.rpm`/tarball automatically (it is gitignored, so it never
lands in source control). This is why end users installing a release get zero-config
login: in CI, both values are injected from a GitHub Actions environment's secrets
(`CLIENT_ID`/`CLIENT_SECRET` in the `Deploy` environment) right before packaging — see
`.github/workflows/release.yml`.

Create your own OAuth client in the
[Google Cloud Console](https://console.cloud.google.com/apis/credentials) with redirect
URI `http://127.0.0.1:8080/` if you don't want to reuse the project's.

> Note the unverified consent screen is capped at 100 users until the app passes
> Google's OAuth verification.

This OAuth flow only covers Google Tasks. Google Keep uses a completely separate,
additional auth path (a Workspace service account, not the Desktop OAuth client above)
that is configured per-Workspace by a Workspace admin, not by the app maintainer — the
setup steps live in the [README](README.md#google-keep-workspace-accounts-only) since
they're deployment-side, not development-side.

## Releases

Pushing a `v*.*.*` tag triggers `.github/workflows/release.yml`, which builds the
Ubuntu and CentOS packages (with the OAuth credentials injected as above) and publishes
them as a GitHub Release.
