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

## Acknowledgements

Feature inspiration directly modeled after the robust work done in [google-task-desktop](https://github.com/codad5/google-task-desktop) by author `codad5`.
