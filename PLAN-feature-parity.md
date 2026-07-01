# Plan: Feature Parity with codad5/google-task-desktop

**Goal:** Implement the missing full CRUD (including subtasks), UI polish, due dates, and starred functionality to match the UX and feature set of `codad5/google-task-desktop`.

## Approach
We will adapt the UX of the referenced Tauri/React app into our C++/ImGui stack by utilizing a 3-pane layout: Sidebar (Lists), Center (Tasks), and Right Panel (Task Details). We will support adding subtasks via a context menu or dedicated button. Starred tasks will be implemented by appending a `[STARRED]` tag to the notes field so it syncs across devices, toggled via a star icon button (★/☆). We will integrate a header-only ImGui date picker for selecting due dates, and ensure destructive actions like "Delete" are styled with red buttons.

*Verdict on `codad5/google-task-desktop`:* Adapt. Its UX (right-panel details, full CRUD) is excellent, but we will adapt it to our C++ ImGui stack rather than adopting its Tauri/React architecture.

## Changes
- `CMakeLists.txt` — Add logic for a 3rd-party ImGui date picker (implemented as `include/imgui_date_picker.h`).
- `src/main.cpp` — 
  - Add a 3-pane layout: Sidebar (Lists), Center (Tasks), Right Panel (Task Details).
  - Add UI controls in the sidebar for "Add List" and context menu for "Delete List".
  - **Center Pane**: Render tasks as a tree. Add an "Add Subtask" action (e.g., via a right-click context menu on a task) that creates a new task with the `parent` ID set.
  - **Center Pane**: Render a star icon (★/☆) directly on the task row that can be clicked to toggle the starred status.
  - **Right Panel**: Implement UI for the selected task to edit title, notes (multi-line), pick a due date, and toggle Starred status.
  - **Right Panel**: Add a **red** "Delete Task" button using ImGui style color pushes.
- `src/api.cpp` / `src/api.h` — Ensure parsing of `notes` handles the `[STARRED]` tag gracefully. Update `create_task` to accept a `parent` ID for subtasks.

## Steps
- [x] Find/integrate a header-only ImGui date picker (`imgui_date_picker.h`).
- [x] Update `main.cpp` to implement a 3-pane layout (Sidebar | Center List | Right Panel).
- [x] Update `main.cpp` Center pane to include an "Add Subtask" option (right-click context menu on a task) that calls `create_task` with the selected task as the `parent`.
- [x] Implement a clickable Star icon (★/☆) inline on the Center pane rows and Right panel to toggle the `[STARRED]` tag in the notes (tag handling centralized in `API::set_starred`/`strip_starred`).
- [x] In the Right Panel, implement UI to edit the task title, notes (multi-line, stripping `[STARRED]`), and due date.
- [x] Ensure the "Delete Task" button in the Right Panel is explicitly styled red using `ImGui::PushStyleColor(ImGuiCol_Button, ...)`.
- [x] Add an "Add List" button to the Sidebar and a right-click context menu on lists to "Delete List".
- [x] Wire up the API calls for List creation/deletion and Task updates.

## Edge cases & risks
- **Notes Tagging**: Users might accidentally edit or delete the `[STARRED]` tag manually from other clients. Our parser checks for the substring anywhere in the notes and gracefully adds/removes it.
- **Subtask Nesting Limits**: Google Tasks API supports subtasks, but we need to ensure the UI tree reflects parent/child relationships correctly after creation and refreshes.
