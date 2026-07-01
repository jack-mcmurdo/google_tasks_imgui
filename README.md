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

## Acknowledgements

Feature inspiration directly modeled after the robust work done in [google-task-desktop](https://github.com/codad5/google-task-desktop) by author `codad5`.
