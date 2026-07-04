#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <functional>
#include <memory>
#include <cstring>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <ctime>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include "api.h"
#include "auth.h"
#include "IconsFontAwesome5.h"
#include "json.hpp"
#include <fstream>

// --- Application State ---
static std::string selected_task_id = "";
static bool focus_title_input = false;
static ImFont* g_header_font = nullptr; // smaller, muted font for sidebar section headers

struct AppState {
    Auth::TokenManager token_manager{"tokens.json"};
    std::string current_account_id;
    std::shared_ptr<API::GoogleTasksAPI> api;
    
    std::vector<API::TaskList> lists;
    std::string selected_list_id;
    
    std::vector<API::Task> tasks;
    
    bool oauth_in_progress = false;
    std::string oauth_error;
    
    std::atomic<bool> needs_refresh{false};
};

// --- Desktop Notifications ---
// The notifier runs on a background thread, so it must never touch the live
// AppState (which the UI thread mutates). Instead the UI thread publishes a
// snapshot of the current tasks under `notify_mutex`, and the notifier reads
// only that snapshot.
static std::mutex notify_mutex;
static std::vector<API::Task> notify_snapshot; // guarded by notify_mutex

static void PublishNotifySnapshot(const std::vector<API::Task>& tasks) {
    std::lock_guard<std::mutex> lock(notify_mutex);
    notify_snapshot = tasks;
}

// Today's local date as "YYYY-MM-DD". Google Tasks stores `due` as a date at
// midnight UTC, so a lexicographic compare of the date portion is enough to tell
// whether a task is due today or overdue.
static std::string TodayDate() {
    time_t t = time(nullptr);
    struct tm tm_local;
    localtime_r(&t, &tm_local);
    char buf[11];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
             tm_local.tm_year + 1900, tm_local.tm_mon + 1, tm_local.tm_mday);
    return std::string(buf);
}

void NotificationThread() {
    std::vector<std::string> notified_ids;
    while (true) {
        std::this_thread::sleep_for(std::chrono::minutes(1));

        std::vector<API::Task> tasks;
        {
            std::lock_guard<std::mutex> lock(notify_mutex);
            tasks = notify_snapshot;
        }

        std::string today = TodayDate();
        for (const auto& task : tasks) {
            if (task.status == "completed" || task.due.length() < 10) continue;

            // Only notify once the due date has been reached (due today or overdue).
            std::string due_date = task.due.substr(0, 10);
            if (due_date > today) continue;

            if (std::find(notified_ids.begin(), notified_ids.end(), task.id) != notified_ids.end()) {
                continue;
            }
            const char* when = (due_date < today) ? "Overdue" : "Due today";
            std::string cmd = "notify-send '" + std::string(when) + "' '" + task.title + "'";
            int ret = system(cmd.c_str());
            (void)ret;
            notified_ids.push_back(task.id);
        }
    }
}

// Load OAuth client credentials from a Google "client_secret.json" file
// (the standard Desktop-app download format, with an "installed" or "web" node).
static bool load_client_secret_file(const std::string& path, std::string& id, std::string& secret) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        nlohmann::json j = nlohmann::json::parse(f);
        const auto& node = j.contains("installed") ? j["installed"]
                         : j.contains("web")       ? j["web"]
                                                   : j;
        id = node.value("client_id", "");
        secret = node.value("client_secret", "");
        return !id.empty() && !secret.empty();
    } catch (...) {
        return false;
    }
}

// Google access tokens expire after ~1 hour. Refresh proactively (with a
// buffer) whenever we're about to use one, so the app doesn't start failing
// API calls mid-session and force a full re-login.
static void EnsureValidToken(Auth::TokenManager& token_manager, Auth::Account& acc) {
    long long now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now < acc.expires_at - 60) return;
    if (Auth::refresh_token_sync(acc)) {
        token_manager.save();
    }
}

// Makes `acc` the active account: refreshes its token, points the API client
// at it, and reloads its task lists. Shared by the sidebar's account click
// handler and the startup default-selection below.
static void SelectAccount(AppState& state, Auth::Account& acc) {
    state.current_account_id = acc.id;
    EnsureValidToken(state.token_manager, acc);
    state.api = std::make_shared<API::GoogleTasksAPI>(acc.access_token);
    state.lists = state.api->get_tasklists();
    state.selected_list_id = "";
    state.tasks.clear();
}

// --- UI Rendering ---

// A thin draggable handle placed between two adjacent panes on the same row.
// Mutates *pane_width directly by mouse delta while dragged, so callers just
// need to feed the (clamped) width back into the next frame's BeginChild.
static void PaneSplitter(const char* str_id, float* pane_width, float min_width, float max_width) {
    ImGui::SameLine(0, 0);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorActive));
    ImGui::Button(str_id, ImVec2(6.0f, -1));
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemActive()) {
        *pane_width += ImGui::GetIO().MouseDelta.x;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    *pane_width = std::clamp(*pane_width, min_width, max_width);
    ImGui::SameLine(0, 0);
}

static void SectionHeaderText(const char* label) {
    // Headers read as hierarchy markers, not content, so they're smaller and
    // more muted than the items they group (Apple/Material sidebar convention),
    // rather than competing with them for attention.
    if (g_header_font) ImGui::PushFont(g_header_font);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", label);
    ImGui::PopStyleColor();
    if (g_header_font) ImGui::PopFont();
}

void RenderSidebar(AppState& state, float width) {
    ImGui::BeginChild("Sidebar", ImVec2(width, 0), true);
    
    if (ImGui::Button("Add Account")) {
        if (!state.oauth_in_progress) {
            state.oauth_in_progress = true;
            state.oauth_error = "";
            Auth::start_oauth_flow([&state](bool success, std::string error_msg, Auth::Account acc) {
                if (success) {
                    state.token_manager.add_account(acc);
                    state.token_manager.save();
                } else {
                    state.oauth_error = error_msg;
                }
                state.oauth_in_progress = false;
            });
        }
    }
    
    if (state.oauth_in_progress) {
        ImGui::Text("Waiting for login in browser...");
    }
    if (!state.oauth_error.empty()) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", state.oauth_error.c_str());
    }

    ImGui::Separator();
    
    SectionHeaderText("Accounts");
    ImGui::Indent();
    auto& accounts = state.token_manager.get_accounts();
    for (auto& acc : accounts) {
        if (ImGui::Selectable(acc.email.c_str(), state.current_account_id == acc.id)) {
            SelectAccount(state, acc);
        }
    }
    ImGui::Unindent();

    ImGui::Separator();

    if (state.api) {
        SectionHeaderText("Lists");
        ImGui::Indent();

        if (ImGui::Selectable(ICON_FA_PLUS "  Add List")) {
            ImGui::OpenPopup("AddListPopup");
        }

        if (ImGui::BeginPopup("AddListPopup")) {
            static char new_list_name[128] = "";
            ImGui::InputText("Name", new_list_name, sizeof(new_list_name));
            if (ImGui::Button("Create")) {
                auto created = state.api->create_tasklist(new_list_name);
                if (created) {
                    state.lists = state.api->get_tasklists();
                    new_list_name[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }

        for (auto it = state.lists.begin(); it != state.lists.end(); ) {
            const auto& list = *it;
            bool selected = (state.selected_list_id == list.id);
            if (ImGui::Selectable((list.title + "##" + list.id).c_str(), selected)) {
                state.selected_list_id = list.id;
                state.needs_refresh = true;
            }

            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::Selectable("Delete List")) {
                    if (state.api->delete_tasklist(list.id)) {
                        state.lists = state.api->get_tasklists();
                        if (state.selected_list_id == list.id) {
                            state.selected_list_id = "";
                            state.tasks.clear();
                        }
                        ImGui::EndPopup();
                        break; // exit loop to avoid invalid iterator
                    }
                }
                ImGui::EndPopup();
            }
            ++it;
        }
        ImGui::Unindent();
    }

    ImGui::EndChild();
}

void AsyncUpdateTask(AppState& state, const API::Task& task) {
    std::thread([api = state.api, list_id = state.selected_list_id, task_copy = task]() {
        if (api) api->update_task(list_id, task_copy);
    }).detach();
}

void AsyncDeleteTask(AppState& state, API::Task* task) {
    if (!task) return;
    task->deleted = true;
    if (selected_task_id == task->id) selected_task_id = "";

    std::thread([api = state.api, list_id = state.selected_list_id, task_id = task->id, &state]() {
        if (api && api->delete_task(list_id, task_id)) {
            state.needs_refresh = true;
        }
    }).detach();
}

void RenderTasks(AppState& state, float width) {
    ImGui::BeginChild("Tasks", ImVec2(width, 0), true);
    
    if (!state.selected_list_id.empty()) {
        if (ImGui::Button("Refresh")) {
            state.needs_refresh = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Task")) {
            API::Task new_task;
            new_task.title = "New Task";
            auto created = state.api->create_task(state.selected_list_id, new_task);
            if (created) {
                state.needs_refresh = true;
                selected_task_id = created->id;
                focus_title_input = true;
            }
        }
        ImGui::Separator();
        
        std::map<std::string, std::vector<API::Task*>> children_map;
        std::vector<API::Task*> root_tasks;
        
        for (auto& task : state.tasks) {
            if (task.deleted) continue;
            if (task.parent.empty()) {
                root_tasks.push_back(&task);
            } else {
                children_map[task.parent].push_back(&task);
            }
        }
        
        std::function<void(API::Task*)> render_task = [&](API::Task* task) {
            ImGui::PushID(task->id.c_str());
            
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (children_map[task->id].empty()) {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }
            if (selected_task_id == task->id) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            
            ImGui::AlignTextToFramePadding();
            bool is_open = ImGui::TreeNodeEx("##node", flags | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_AllowOverlap);
            if (ImGui::IsItemClicked()) {
                selected_task_id = task->id;
            }

            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("TASK_ID", task->id.c_str(), task->id.size() + 1);
                ImGui::Text("Move: %s", task->title.c_str());
                ImGui::EndDragDropSource();
            }
            
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TASK_ID")) {
                    std::string dragged_id = (const char*)payload->Data;
                    if (dragged_id != task->id) {
                        state.api->move_task(state.selected_list_id, dragged_id, task->id);
                        state.needs_refresh = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::Selectable("Add Subtask")) {
                    API::Task subtask;
                    subtask.title = "New Subtask";
                    auto created = state.api->create_task(state.selected_list_id, subtask, task->id);
                    if (created) {
                        state.needs_refresh = true;
                        selected_task_id = created->id;
                        focus_title_input = true;
                    }
                }
                if (!task->parent.empty()) {
                    std::string grandparent;
                    for (auto& t : state.tasks) {
                        if (t.id == task->parent) { grandparent = t.parent; break; }
                    }
                    if (!grandparent.empty() && ImGui::Selectable("Move Up One Level")) {
                        state.api->move_task(state.selected_list_id, task->id, grandparent);
                        state.needs_refresh = true;
                    }
                    if (ImGui::Selectable("Move to Top Level")) {
                        state.api->move_task(state.selected_list_id, task->id);
                        state.needs_refresh = true;
                    }
                }
                if (ImGui::Selectable("Delete Task")) {
                    AsyncDeleteTask(state, task);
                }
                ImGui::EndPopup();
            }

            ImGui::SameLine();
            
            bool completed = (task->status == "completed");
            if (ImGui::Checkbox("##completed", &completed)) {
                task->status = completed ? "completed" : "needsAction";
                AsyncUpdateTask(state, *task);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Mark as %s", completed ? "needs action" : "completed");
            }
            ImGui::SameLine();
            
            bool is_starred = API::task_is_starred(task->notes);
            ImVec4 star_tint = is_starred ? ImVec4(1.00f, 0.84f, 0.00f, 1.00f) : ImVec4(0.55f, 0.55f, 0.58f, 1.00f);
            ImGui::PushStyleColor(ImGuiCol_Text, star_tint);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(star_tint.x, star_tint.y, star_tint.z, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(star_tint.x, star_tint.y, star_tint.z, 0.28f));
            if (ImGui::Button(ICON_FA_STAR "##star")) {
                task->notes = API::set_starred(task->notes, !is_starred);
                AsyncUpdateTask(state, *task);
            }
            ImGui::PopStyleColor(4);
            ImGui::SameLine();
            
            float trash_button_width = ImGui::CalcTextSize(ICON_FA_TRASH).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            float available_width = ImGui::GetContentRegionAvail().x;
            float title_width = available_width - trash_button_width - ImGui::GetStyle().ItemSpacing.x;

            // The tree node above already spans the full row and owns the row's
            // hover/selected highlight. Rendering the title as plain text (instead
            // of a second interactive widget stacked on top of the tree node) means
            // there's nothing here to compete for hover, so the whole row lights up
            // uniformly instead of flickering between "row" and "text" highlights.
            float row_height = ImGui::GetFrameHeight();
            ImVec2 text_pos = ImGui::GetCursorScreenPos();
            text_pos.y += (row_height - ImGui::GetTextLineHeight()) * 0.5f;
            ImGui::PushClipRect(text_pos, ImVec2(text_pos.x + title_width, text_pos.y + ImGui::GetTextLineHeight()), true);
            ImGui::GetWindowDrawList()->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), task->title.c_str());
            ImGui::PopClipRect();
            ImGui::Dummy(ImVec2(title_width, row_height));

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.87f, 0.40f, 0.40f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.87f, 0.30f, 0.30f, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.87f, 0.30f, 0.30f, 0.28f));
            if (ImGui::Button(ICON_FA_TRASH "##trash")) {
                AsyncDeleteTask(state, task);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Delete Task");
            }
            ImGui::PopStyleColor(4);
            
            if (is_open) {
                for (auto* child : children_map[task->id]) {
                    render_task(child);
                }
                ImGui::TreePop();
            }
            
            ImGui::PopID();
        };
        
        for (auto* root_task : root_tasks) {
            render_task(root_task);
        }
        
    } else {
        ImGui::Text("Select a list to view tasks.");
    }
    
    ImGui::EndChild();
}

#include "imgui_date_picker.h"

void RenderRightPanel(AppState& state) {
    ImGui::BeginChild("RightPanel", ImVec2(0, 0), true);
    if (selected_task_id.empty()) {
        ImGui::Text("Select a task to view details.");
    } else {
        API::Task* task = nullptr;
        for (auto& t : state.tasks) {
            if (t.id == selected_task_id) {
                task = &t;
                break;
            }
        }
        
        if (task) {
            char title_buf[256];
            strncpy(title_buf, task->title.c_str(), sizeof(title_buf) - 1);
            title_buf[sizeof(title_buf) - 1] = '\0';
            
            ImGui::TextUnformatted("Title");
            if (focus_title_input) {
                ImGui::SetKeyboardFocusHere();
                focus_title_input = false;
            }
            if (ImGui::InputText("##Title", title_buf, sizeof(title_buf), ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()) {
                task->title = title_buf;
                AsyncUpdateTask(state, *task);
            }
            
            bool is_starred = API::task_is_starred(task->notes);
            ImVec4 star_tint = is_starred ? ImVec4(1.00f, 0.84f, 0.00f, 1.00f) : ImVec4(0.55f, 0.55f, 0.58f, 1.00f);
            ImGui::PushStyleColor(ImGuiCol_Text, star_tint);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(star_tint.x, star_tint.y, star_tint.z, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(star_tint.x, star_tint.y, star_tint.z, 0.28f));
            if (ImGui::Button(ICON_FA_STAR " Starred##rp")) {
                task->notes = API::set_starred(task->notes, !is_starred);
                AsyncUpdateTask(state, *task);
            }
            ImGui::PopStyleColor(4);

            std::string display_notes = API::strip_starred(task->notes);

            char notes_buf[1024];
            strncpy(notes_buf, display_notes.c_str(), sizeof(notes_buf) - 1);
            notes_buf[sizeof(notes_buf) - 1] = '\0';
            ImGui::TextUnformatted("Notes");
            if (ImGui::InputTextMultiline("##Notes", notes_buf, sizeof(notes_buf), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 8)) || ImGui::IsItemDeactivatedAfterEdit()) {
                task->notes = API::set_starred(notes_buf, is_starred);
                AsyncUpdateTask(state, *task);
            }

            std::string due_date = task->due;
            std::string short_due = due_date.length() >= 10 ? due_date.substr(0, 10) : "";
            if (ImGui::DatePicker("Due Date", short_due)) {
                task->due = short_due.empty() ? "" : short_due + "T00:00:00.000Z";
                AsyncUpdateTask(state, *task);
            }
            
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.87f, 0.30f, 0.30f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.93f, 0.38f, 0.38f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.22f, 0.22f, 1.00f));
            if (ImGui::Button(ICON_FA_TRASH " Delete Task", ImVec2(-FLT_MIN, 0))) {
                AsyncDeleteTask(state, task);
            }
            ImGui::PopStyleColor(3);
        }
    }
    ImGui::EndChild();
}

int main(int argc, char** argv) {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1024, 768, "Google Tasks ImGui", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    // Shape: rounded, borderless surfaces read as "designed" rather than boxed-in ImGui defaults.
    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;

    // Spacing: roomier touch-friendly padding instead of default cramped ImGui density.
    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 10.0f;

    // Palette: dark surface tiers + indigo accent (Material-ish) instead of default purple.
    ImVec4 primary        = ImVec4(0.36f, 0.42f, 0.88f, 1.00f);
    ImVec4 primary_hover   = ImVec4(0.46f, 0.52f, 0.95f, 1.00f);
    ImVec4 primary_active  = ImVec4(0.30f, 0.36f, 0.80f, 1.00f);

    style.Colors[ImGuiCol_Text] = ImVec4(0.93f, 0.93f, 0.95f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.55f, 0.58f, 1.00f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.13f, 0.16f, 1.00f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.24f, 0.24f, 0.28f, 0.60f);

    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.21f, 1.00f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.26f, 0.30f, 1.00f);

    style.Colors[ImGuiCol_Header] = ImVec4(primary.x, primary.y, primary.z, 0.30f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(primary.x, primary.y, primary.z, 0.45f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(primary.x, primary.y, primary.z, 0.60f);

    style.Colors[ImGuiCol_Button] = primary;
    style.Colors[ImGuiCol_ButtonHovered] = primary_hover;
    style.Colors[ImGuiCol_ButtonActive] = primary_active;

    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.90f, 0.91f, 0.98f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab] = primary;
    style.Colors[ImGuiCol_SliderGrabActive] = primary_active;

    style.Colors[ImGuiCol_Separator] = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.34f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.38f, 0.38f, 0.42f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.46f, 0.46f, 0.50f, 1.00f);

    // Oversample + snap for crisper text edges, and a slightly larger base size.
    ImFontConfig main_font_config;
    main_font_config.OversampleH = 3;
    main_font_config.OversampleV = 3;
    main_font_config.PixelSnapH = true;

    if (FILE* f = fopen("assets/fonts/PTSans-Regular.ttf", "r")) {
        fclose(f);
        io.Fonts->AddFontFromFileTTF("assets/fonts/PTSans-Regular.ttf", 20.0f, &main_font_config);
    } else if (FILE* f = fopen("../assets/fonts/PTSans-Regular.ttf", "r")) {
        fclose(f);
        io.Fonts->AddFontFromFileTTF("../assets/fonts/PTSans-Regular.ttf", 20.0f, &main_font_config);
    } else if (FILE* f = fopen("/usr/share/google-tasks-imgui/fonts/PTSans-Regular.ttf", "r")) {
        fclose(f);
        io.Fonts->AddFontFromFileTTF("/usr/share/google-tasks-imgui/fonts/PTSans-Regular.ttf", 20.0f, &main_font_config);
    }

    ImFontConfig config;
    config.MergeMode = true;
    config.PixelSnapH = true;
    config.GlyphMinAdvanceX = 16.0f;
    static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };

    if (FILE* f = fopen("assets/fonts/fa-solid-900.ttf", "r")) {
        fclose(f);
        io.Fonts->AddFontFromFileTTF("assets/fonts/fa-solid-900.ttf", 16.0f, &config, icon_ranges);
    } else if (FILE* f = fopen("../assets/fonts/fa-solid-900.ttf", "r")) {
        fclose(f);
        io.Fonts->AddFontFromFileTTF("../assets/fonts/fa-solid-900.ttf", 16.0f, &config, icon_ranges);
    } else if (FILE* f = fopen("/usr/share/google-tasks-imgui/fonts/fa-solid-900.ttf", "r")) {
        fclose(f);
        io.Fonts->AddFontFromFileTTF("/usr/share/google-tasks-imgui/fonts/fa-solid-900.ttf", 16.0f, &config, icon_ranges);
    }

    // Separate, smaller font for sidebar section headers ("Accounts", "Lists"),
    // paired with a muted text color, so headers read as subordinate group labels
    // rather than competing in size with the items they contain.
    if (FILE* f = fopen("assets/fonts/PTSans-Regular.ttf", "r")) {
        fclose(f);
        g_header_font = io.Fonts->AddFontFromFileTTF("assets/fonts/PTSans-Regular.ttf", 14.0f, &main_font_config);
    } else if (FILE* f = fopen("../assets/fonts/PTSans-Regular.ttf", "r")) {
        fclose(f);
        g_header_font = io.Fonts->AddFontFromFileTTF("../assets/fonts/PTSans-Regular.ttf", 14.0f, &main_font_config);
    } else if (FILE* f = fopen("/usr/share/google-tasks-imgui/fonts/PTSans-Regular.ttf", "r")) {
        fclose(f);
        g_header_font = io.Fonts->AddFontFromFileTTF("/usr/share/google-tasks-imgui/fonts/PTSans-Regular.ttf", 14.0f, &main_font_config);
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    AppState state;
    state.token_manager.load();

    // Default to the first account and its first list so returning users land
    // straight on their tasks instead of an empty sidebar.
    auto& startup_accounts = state.token_manager.get_accounts();
    if (!startup_accounts.empty()) {
        SelectAccount(state, startup_accounts.front());
        if (!state.lists.empty()) {
            state.selected_list_id = state.lists.front().id;
            state.needs_refresh = true;
        }
    }

    std::string client_id, client_secret;
    if (const char* e = std::getenv("GOOGLE_CLIENT_ID")) client_id = e;
    if (const char* e = std::getenv("GOOGLE_CLIENT_SECRET")) client_secret = e;
    if (client_id.empty() || client_secret.empty()) {
        load_client_secret_file("client_secret.json", client_id, client_secret) ||
        load_client_secret_file("../client_secret.json", client_id, client_secret) ||
        load_client_secret_file("/usr/share/google-tasks-imgui/client_secret.json", client_id, client_secret) ||
        load_client_secret_file("/usr/local/share/google-tasks-imgui/client_secret.json", client_id, client_secret);
    }

    if (!client_id.empty() && !client_secret.empty()) {
        Auth::set_client_credentials(client_id, client_secret);
    } else {
        std::cerr << "No OAuth credentials found. Set GOOGLE_CLIENT_ID and "
                     "GOOGLE_CLIENT_SECRET, or place a client_secret.json next to the binary."
                  << std::endl;
    }

    std::thread notifier(NotificationThread);
    notifier.detach();

    auto last_token_check = std::chrono::steady_clock::now();

    float sidebar_width = 250.0f;
    float tasks_width = 450.0f;
    const float min_pane_width = 150.0f;
    const float splitter_thickness = 6.0f;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        auto now_steady = std::chrono::steady_clock::now();
        if (now_steady - last_token_check > std::chrono::seconds(60)) {
            last_token_check = now_steady;
            if (!state.current_account_id.empty()) {
                if (auto* acc = state.token_manager.get_account(state.current_account_id)) {
                    EnsureValidToken(state.token_manager, *acc);
                    if (state.api) state.api->set_access_token(acc->access_token);
                }
            }
        }

        if (state.needs_refresh) {
            if (state.api && !state.selected_list_id.empty()) {
                state.tasks = state.api->get_tasks(state.selected_list_id);
            }
            state.needs_refresh = false;
            PublishNotifySnapshot(state.tasks);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

        // Re-clamp against the current window size (not just the drag delta) so a
        // shrunk OS window can't leave the right panel with negative width.
        float total_avail = ImGui::GetContentRegionAvail().x;
        float max_sidebar_width = std::max(min_pane_width, total_avail - tasks_width - min_pane_width - splitter_thickness * 2.0f);
        sidebar_width = std::clamp(sidebar_width, min_pane_width, max_sidebar_width);
        float max_tasks_width = std::max(min_pane_width, total_avail - sidebar_width - min_pane_width - splitter_thickness * 2.0f);
        tasks_width = std::clamp(tasks_width, min_pane_width, max_tasks_width);

        RenderSidebar(state, sidebar_width);
        PaneSplitter("##SidebarSplitter", &sidebar_width, min_pane_width, max_sidebar_width);
        RenderTasks(state, tasks_width);
        PaneSplitter("##TasksSplitter", &tasks_width, min_pane_width, max_tasks_width);
        RenderRightPanel(state);

        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.15f, 0.15f, 0.15f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
