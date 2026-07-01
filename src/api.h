#pragma once

#include <string>
#include <vector>
#include <optional>

namespace API {

struct TaskList {
    std::string id;
    std::string title;
    std::string updated;
};

struct Task {
    std::string id;
    std::string title;
    std::string notes;
    std::string status; // "needsAction" or "completed"
    std::string due;
    std::string completed;
    bool deleted = false;
    bool hidden = false;
    std::string parent; // ID of parent task
    std::string position;
};

class GoogleTasksAPI {
public:
    GoogleTasksAPI(const std::string& access_token);
    
    void set_access_token(const std::string& token);

    // Task Lists
    std::vector<TaskList> get_tasklists();
    std::optional<TaskList> get_tasklist(const std::string& list_id);
    std::optional<TaskList> create_tasklist(const std::string& title);
    std::optional<TaskList> update_tasklist(const std::string& list_id, const std::string& new_title);
    bool delete_tasklist(const std::string& list_id);

    // Tasks
    std::vector<Task> get_tasks(const std::string& list_id, bool show_completed = true, bool show_hidden = true);
    std::optional<Task> get_task(const std::string& list_id, const std::string& task_id);
    std::optional<Task> create_task(const std::string& list_id, const Task& task, const std::string& parent = "", const std::string& previous = "");
    std::optional<Task> update_task(const std::string& list_id, const Task& task);
    bool delete_task(const std::string& list_id, const std::string& task_id);
    
    // Move task
    std::optional<Task> move_task(const std::string& list_id, const std::string& task_id, const std::string& parent = "", const std::string& previous = "");

private:
    std::string access_token_;
};

} // namespace API
