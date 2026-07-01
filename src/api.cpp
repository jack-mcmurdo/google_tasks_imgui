#include "api.h"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"
#include <iostream>

using json = nlohmann::json;

namespace API {

static TaskList parse_tasklist(const json& j) {
    TaskList list;
    if (j.contains("id") && j["id"].is_string()) list.id = j["id"].get<std::string>();
    if (j.contains("title") && j["title"].is_string()) list.title = j["title"].get<std::string>();
    if (j.contains("updated") && j["updated"].is_string()) list.updated = j["updated"].get<std::string>();
    return list;
}

static Task parse_task(const json& j) {
    Task task;
    if (j.contains("id") && j["id"].is_string()) task.id = j["id"].get<std::string>();
    if (j.contains("title") && j["title"].is_string()) task.title = j["title"].get<std::string>();
    if (j.contains("notes") && j["notes"].is_string()) task.notes = j["notes"].get<std::string>();
    if (j.contains("status") && j["status"].is_string()) task.status = j["status"].get<std::string>();
    if (j.contains("due") && j["due"].is_string()) task.due = j["due"].get<std::string>();
    if (j.contains("completed") && j["completed"].is_string()) task.completed = j["completed"].get<std::string>();
    if (j.contains("deleted") && j["deleted"].is_boolean()) task.deleted = j["deleted"].get<bool>();
    if (j.contains("hidden") && j["hidden"].is_boolean()) task.hidden = j["hidden"].get<bool>();
    if (j.contains("parent") && j["parent"].is_string()) task.parent = j["parent"].get<std::string>();
    if (j.contains("position") && j["position"].is_string()) task.position = j["position"].get<std::string>();
    return task;
}

// deleted/hidden/completed are output-only in the Google Tasks API, so we never
// send them. On update we must send notes/due explicitly (even when empty) so
// that clearing them — e.g. un-starring or removing a due date — actually syncs.
static json task_to_json(const Task& task, bool for_update = false) {
    json j;
    if (!task.id.empty()) j["id"] = task.id;
    j["title"] = task.title;
    if (!task.status.empty()) j["status"] = task.status;

    if (for_update) {
        j["notes"] = task.notes;
        if (task.due.empty()) j["due"] = nullptr;
        else j["due"] = task.due;
    } else {
        if (!task.notes.empty()) j["notes"] = task.notes;
        if (!task.due.empty()) j["due"] = task.due;
    }
    return j;
}

GoogleTasksAPI::GoogleTasksAPI(const std::string& access_token) : access_token_(access_token) {}

void GoogleTasksAPI::set_access_token(const std::string& token) {
    access_token_ = token;
}

std::vector<TaskList> GoogleTasksAPI::get_tasklists() {
    httplib::Client cli("https://tasks.googleapis.com");
    cli.set_bearer_token_auth(access_token_);
    
    std::vector<TaskList> lists;
    std::string page_token = "";
    
    do {
        std::string path = "/tasks/v1/users/@me/lists?maxResults=100";
        if (!page_token.empty()) {
            path += "&pageToken=" + page_token;
        }
        
        auto res = cli.Get(path.c_str());
        if (res && res->status == 200) {
            json j = json::parse(res->body);
            if (j.contains("items") && j["items"].is_array()) {
                for (const auto& item : j["items"]) {
                    lists.push_back(parse_tasklist(item));
                }
            }
            if (j.contains("nextPageToken") && j["nextPageToken"].is_string()) {
                page_token = j["nextPageToken"].get<std::string>();
            } else {
                page_token = "";
            }
        } else {
            std::cerr << "Failed to fetch task lists. Status: " << (res ? std::to_string(res->status) : "null") << std::endl;
            if (res) std::cerr << "Body: " << res->body << std::endl;
            break;
        }
    } while (!page_token.empty());
    
    return lists;
}

std::optional<TaskList> GoogleTasksAPI::get_tasklist(const std::string& list_id) {
    httplib::Client cli("https://tasks.googleapis.com");
    cli.set_bearer_token_auth(access_token_);
    
    auto res = cli.Get("/tasks/v1/users/@me/lists/" + list_id);
    if (res && res->status == 200) {
        return parse_tasklist(json::parse(res->body));
    }
    return std::nullopt;
}

std::optional<TaskList> GoogleTasksAPI::create_tasklist(const std::string& title) {
    httplib::Client cli("https://tasks.googleapis.com");
    cli.set_bearer_token_auth(access_token_);
    
    json j;
    j["title"] = title;
    std::string body = j.dump();
    
    auto res = cli.Post("/tasks/v1/users/@me/lists", body, "application/json");
    if (res && res->status == 200) {
        return parse_tasklist(json::parse(res->body));
    }
    return std::nullopt;
}

std::optional<TaskList> GoogleTasksAPI::update_tasklist(const std::string& list_id, const std::string& new_title) {
    httplib::Client cli("https://tasks.googleapis.com");
    cli.set_bearer_token_auth(access_token_);
    
    json j;
    j["title"] = new_title;
    std::string body = j.dump();
    
    auto res = cli.Put("/tasks/v1/users/@me/lists/" + list_id, body, "application/json");
    if (res && res->status == 200) {
        return parse_tasklist(json::parse(res->body));
    }
    return std::nullopt;
}

bool GoogleTasksAPI::delete_tasklist(const std::string& list_id) {
    httplib::Client cli("https://tasks.googleapis.com");
    cli.set_bearer_token_auth(access_token_);
    
    auto res = cli.Delete("/tasks/v1/users/@me/lists/" + list_id);
    return res && res->status == 204;
}

std::vector<Task> GoogleTasksAPI::get_tasks(const std::string& list_id, bool show_completed, bool show_hidden) {
    httplib::Client cli("https://tasks.googleapis.com");
    cli.set_bearer_token_auth(access_token_);
    
    std::vector<Task> tasks;
    std::string page_token = "";
    
    do {
        std::string path = "/tasks/v1/lists/" + list_id + "/tasks?maxResults=100";
        if (!show_completed) path += "&showCompleted=false";
        if (!show_hidden) path += "&showHidden=false";
        if (!page_token.empty()) path += "&pageToken=" + page_token;
        
        auto res = cli.Get(path.c_str());
        if (res && res->status == 200) {
            json j = json::parse(res->body);
            if (j.contains("items") && j["items"].is_array()) {
                for (const auto& item : j["items"]) {
                    tasks.push_back(parse_task(item));
                }
            }
            if (j.contains("nextPageToken") && j["nextPageToken"].is_string()) {
                page_token = j["nextPageToken"].get<std::string>();
            } else {
                page_token = "";
            }
        } else {
            break;
        }
    } while (!page_token.empty());
    
    return tasks;
}

std::optional<Task> GoogleTasksAPI::get_task(const std::string& list_id, const std::string& task_id) {
    httplib::Client cli("https://tasks.googleapis.com");
    cli.set_bearer_token_auth(access_token_);
    
    auto res = cli.Get("/tasks/v1/lists/" + list_id + "/tasks/" + task_id);
    if (res && res->status == 200) {
        return parse_task(json::parse(res->body));
    }
    return std::nullopt;
}

std::optional<Task> GoogleTasksAPI::create_task(const std::string& list_id, const Task& task, const std::string& parent, const std::string& previous) {
    httplib::Client cli("https://tasks.googleapis.com");
    cli.set_bearer_token_auth(access_token_);
    
    std::string path = "/tasks/v1/lists/" + list_id + "/tasks";
    bool has_query = false;
    if (!parent.empty()) {
        path += "?parent=" + parent;
        has_query = true;
    }
    if (!previous.empty()) {
        path += (has_query ? "&" : "?");
        path += "previous=" + previous;
    }
    
    json j = task_to_json(task);
    std::string body = j.dump();
    
    auto res = cli.Post(path.c_str(), body, "application/json");
    if (res && res->status == 200) {
        return parse_task(json::parse(res->body));
    }
    return std::nullopt;
}

std::optional<Task> GoogleTasksAPI::update_task(const std::string& list_id, const Task& task) {
    if (task.id.empty()) return std::nullopt;

    httplib::Client cli("https://tasks.googleapis.com");
    cli.set_bearer_token_auth(access_token_);
    
    json j = task_to_json(task, /*for_update=*/true);
    std::string body = j.dump();

    // PATCH honours partial updates and null-clears, and leaves position/parent untouched.
    auto res = cli.Patch("/tasks/v1/lists/" + list_id + "/tasks/" + task.id, body, "application/json");
    if (res && res->status == 200) {
        return parse_task(json::parse(res->body));
    }
    return std::nullopt;
}

bool GoogleTasksAPI::delete_task(const std::string& list_id, const std::string& task_id) {
    httplib::Client cli("https://tasks.googleapis.com");
    cli.set_bearer_token_auth(access_token_);
    
    auto res = cli.Delete("/tasks/v1/lists/" + list_id + "/tasks/" + task_id);
    return res && res->status == 204;
}

std::optional<Task> GoogleTasksAPI::move_task(const std::string& list_id, const std::string& task_id, const std::string& parent, const std::string& previous) {
    httplib::Client cli("https://tasks.googleapis.com");
    cli.set_bearer_token_auth(access_token_);
    
    std::string path = "/tasks/v1/lists/" + list_id + "/tasks/" + task_id + "/move";
    bool has_query = false;
    if (!parent.empty()) {
        path += "?parent=" + parent;
        has_query = true;
    }
    if (!previous.empty()) {
        path += (has_query ? "&" : "?");
        path += "previous=" + previous;
    }
    
    auto res = cli.Post(path.c_str(), "", "application/json"); // Body can be empty for move
    if (res && res->status == 200) {
        return parse_task(json::parse(res->body));
    }
    return std::nullopt;
}

} // namespace API
