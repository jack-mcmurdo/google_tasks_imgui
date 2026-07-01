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

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include "api.h"
#include "auth.h"

// --- Application State ---
static char edit_buffer[256] = "";
static std::string editing_task_id = "";

struct AppState {
    Auth::TokenManager token_manager{"tokens.json"};
    std::string current_account_id;
    std::unique_ptr<API::GoogleTasksAPI> api;
    
    std::vector<API::TaskList> lists;
    std::string selected_list_id;
    
    std::vector<API::Task> tasks;
    
    bool oauth_in_progress = false;
    std::string oauth_error;
    
    bool needs_refresh = false;
};

// --- Desktop Notifications ---
std::mutex notify_mutex;
std::vector<API::Task> notified_tasks;

void NotificationThread(AppState* state) {
    while (true) {
        std::this_thread::sleep_for(std::chrono::minutes(1));
        if (!state->api || state->selected_list_id.empty()) continue;
        
        std::lock_guard<std::mutex> lock(notify_mutex);
        for (const auto& task : state->tasks) {
            if (task.status != "completed" && !task.due.empty()) {
                bool already_notified = false;
                for (const auto& n : notified_tasks) {
                    if (n.id == task.id) {
                        already_notified = true;
                        break;
                    }
                }
                
                if (!already_notified) {
                    std::string cmd = "notify-send 'Task Due' '" + task.title + "'";
                    int ret = system(cmd.c_str());
                    (void)ret;
                    notified_tasks.push_back(task);
                }
            }
        }
    }
}

// --- UI Rendering ---

void RenderSidebar(AppState& state) {
    ImGui::BeginChild("Sidebar", ImVec2(250, 0), true);
    
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
    
    ImGui::Text("Accounts");
    auto& accounts = state.token_manager.get_accounts();
    for (auto& acc : accounts) {
        if (ImGui::Selectable(acc.email.c_str(), state.current_account_id == acc.id)) {
            state.current_account_id = acc.id;
            state.api = std::make_unique<API::GoogleTasksAPI>(acc.access_token);
            state.lists = state.api->get_tasklists();
            state.selected_list_id = "";
            state.tasks.clear();
        }
    }
    
    ImGui::Separator();
    
    if (state.api) {
        ImGui::Text("Lists");
        for (const auto& list : state.lists) {
            if (ImGui::Selectable(list.title.c_str(), state.selected_list_id == list.id)) {
                state.selected_list_id = list.id;
                state.needs_refresh = true;
            }
        }
    }

    ImGui::EndChild();
}

void RenderTasks(AppState& state) {
    ImGui::BeginChild("Tasks", ImVec2(0, 0), true);
    
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
            }
        }
        ImGui::Separator();
        
        std::map<std::string, std::vector<API::Task*>> children_map;
        std::vector<API::Task*> root_tasks;
        
        for (auto& task : state.tasks) {
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
            
            bool is_open = ImGui::TreeNodeEx("##node", flags);
            ImGui::SameLine();
            
            bool completed = (task->status == "completed");
            if (ImGui::Checkbox("##completed", &completed)) {
                task->status = completed ? "completed" : "needsAction";
                state.api->update_task(state.selected_list_id, *task);
            }
            ImGui::SameLine();
            
            if (editing_task_id == task->id) {
                ImGui::SetNextItemWidth(300);
                if (ImGui::InputText("##edit", edit_buffer, sizeof(edit_buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    task->title = edit_buffer;
                    state.api->update_task(state.selected_list_id, *task);
                    editing_task_id = "";
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    editing_task_id = "";
                }
            } else {
                ImGui::Text("%s", task->title.c_str());
                if (ImGui::IsItemClicked()) {
                    editing_task_id = task->id;
                    strncpy(edit_buffer, task->title.c_str(), sizeof(edit_buffer) - 1);
                }
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

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    AppState state;
    state.token_manager.load();
    
    const char* client_id = std::getenv("GOOGLE_CLIENT_ID");
    const char* client_secret = std::getenv("GOOGLE_CLIENT_SECRET");
    if (client_id && client_secret) {
        Auth::set_client_credentials(client_id, client_secret);
    }

    std::thread notifier(NotificationThread, &state);
    notifier.detach();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (state.needs_refresh) {
            if (state.api && !state.selected_list_id.empty()) {
                state.tasks = state.api->get_tasks(state.selected_list_id);
            }
            state.needs_refresh = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
        
        RenderSidebar(state);
        ImGui::SameLine();
        RenderTasks(state);
        
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
