# Plan: Google Tasks C++ App (ImGui Edition)

## PROGRESS Instructions for Future Agents
- Each new agent must pick a pending task from the Milestones below.
- Only when the tests and compilation are clean, can that task be marked as done (`[x]`).
- After marking a task as done, commit that milestone/task to the repository.
- Tasks requiring human intervention are listed in the "Tasks Requiring Human Intervention" subsection below. Leave all such tasks until all development is done.

### Tasks Requiring Human Intervention
- [ ] **Google Cloud Setup**: Create an OAuth 2.0 Client ID (Desktop Application type) with redirect URI `http://127.0.0.1:8080/`.


**Goal:** Build a fast, standalone, and feature-rich C++ desktop application using Dear ImGui to manage Google Tasks, featuring seamless system-browser OAuth login, advanced task features (subtasks, drag-and-drop), and automated cross-platform release pipelines.

## Approach
- **UI & Graphics**: Dear ImGui (via GLFW/OpenGL3 backend) included as a git submodule to keep the codebase self-contained. 
- **Networking**: `yhirose/cpp-httplib` (header-only) to handle REST API calls and the local OAuth callback server.
- **Data Parsing**: `nlohmann/json` (header-only).
- **CI/CD Build System**: GitHub Actions utilizing custom Docker containers (Ubuntu 24.04 and CentOS Stream 9) to securely build and output `.deb`, `.rpm`, and portable standalone binaries via CMake/CPack.
- **Inspiration**: Feature parity inspired by [codad5/google-task-desktop](https://github.com/codad5/google-task-desktop).

## Milestones

### Milestone 1: Repository & Tooling Setup
- [x] **Git & Submodules**: Initialize the repo, create a standard C++ `.gitignore`, and add `imgui` as a git submodule (alongside its `backends/` folder).
- [x] **IDE Configuration**: 
  - Create `.vscode/settings.json`, `tasks.json`, and `launch.json` for CMake integration and debugging.
  - Create `.zed/settings.json` configuring `clangd` for auto-formatting and C++ language server support.

### Milestone 2: CI/CD & Build Infrastructure
- [x] **Docker Environments**: 
  - `docker/ubuntu2404.Dockerfile`: Base Ubuntu 24.04 image with `build-essential`, `cmake`, `libglfw3-dev`, `libssl-dev`.
  - `docker/centos9.Dockerfile`: Base CentOS Stream 9 image with `gcc-c++`, `cmake`, `glfw-devel`, `openssl-devel`, `rpm-build`.
- [x] **CMake Configuration**: Configure `CMakeLists.txt` to compile ImGui, link the header-only libraries, link OpenSSL, and use `CPack` to define `.deb` and `.rpm` packaging metadata.
- [x] **GitHub Actions Workflow**: Create `.github/workflows/release.yml`. Trigger on `tags: ['v*.*.*']`. Spin up the Docker containers, compile the binary, run CPack, and upload the resulting `.deb`, `.rpm`, and raw standalone binary to the GitHub Release.

### Milestone 3: Core Backend & Auth
- [x] **Multi-Account OAuth Loop**: 
  - Use system commands (`xdg-open` / `open`) to launch the Google Login URL.
  - Run `httplib::Server` on port 8080 (on a separate thread) to catch the redirect code.
  - Exchange the code for tokens via `httplib::Client`.
  - Implement a token manager that can store, retrieve, and swap tokens for multiple Google accounts.

### Milestone 4: Google Tasks API Integration
- [x] **API Client Class**: Implement wrappers around `cpp-httplib` to parse JSON from:
  - Fetching Task Lists
  - Fetching, Creating, Updating, and Deleting Tasks (Full CRUD)
  - Moving tasks (to support reordering / subtask assignment via the `parent` and `previous` API parameters).
  - Updating "Starred" status (if mapped to a specific list/property) and Due Dates.

### Milestone 5: ImGui Frontend
- [x] **Main Layout**: Design a modern, clean interface with a collapsible left sidebar (for Accounts and Lists) and a main content pane (for Tasks).
- [x] **Task Interactions**:
  - Implement **Inline Editing**: Clicking a task title swaps it to an `ImGui::InputText`.
  - Implement **Subtasks**: Render tasks in a tree format using `ImGui::TreeNode`.
  - Implement **Drag-and-Drop**: Use `ImGui::BeginDragDropSource()` and `ImGui::BeginDragDropTarget()` to reorder tasks and nest them as subtasks.
- [x] **Desktop Notifications**: Integrate a background thread that periodically checks due dates and triggers OS-level notifications (e.g., via `libnotify` via `system()` on Linux).

### Milestone 6: Documentation
- [ ] **README.md**: Document build instructions for both Linux targets. Include an **Acknowledgements** section explicitly crediting [codad5/google-task-desktop](https://github.com/codad5/google-task-desktop) by author `codad5` as the feature inspiration.

## Edge cases & risks
- **ImGui Drag-and-Drop**: Managing the UI state for dragging a task from one part of a tree and nesting it inside another requires careful synchronization with the Google API `parent` hierarchy.
- **OpenSSL Compatibility**: Statically linking OpenSSL for the standalone binary across different Linux distributions can be problematic. The `.deb` and `.rpm` packages are safer as they rely on the system's dynamic OpenSSL libraries.
- **OAuth Thread Safety**: `httplib::Server` is blocking. The OAuth callback server must cleanly unblock and terminate once the redirect is caught to avoid freezing the ImGui render loop.

## Acknowledgements 
*Feature inspiration directly modeled after the robust work done in [google-task-desktop](https://github.com/codad5/google-task-desktop) by [codad5].*
