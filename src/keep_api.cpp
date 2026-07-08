#include "keep_api.h"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"
#include <iostream>

using json = nlohmann::json;

namespace API {

static KeepNote parse_note(const json& j) {
    KeepNote note;
    if (j.contains("name") && j["name"].is_string()) note.name = j["name"].get<std::string>();
    if (j.contains("title") && j["title"].is_string()) note.title = j["title"].get<std::string>();
    if (j.contains("createTime") && j["createTime"].is_string()) note.create_time = j["createTime"].get<std::string>();
    if (j.contains("updateTime") && j["updateTime"].is_string()) note.update_time = j["updateTime"].get<std::string>();
    if (j.contains("trashed") && j["trashed"].is_boolean()) note.trashed = j["trashed"].get<bool>();

    if (j.contains("body") && j["body"].is_object()) {
        const auto& body = j["body"];
        if (body.contains("list") && body["list"].is_object()) {
            note.is_list = true;
            const auto& list = body["list"];
            if (list.contains("listItems") && list["listItems"].is_array()) {
                for (const auto& item : list["listItems"]) {
                    KeepNoteItem note_item;
                    if (item.contains("text") && item["text"].is_object() &&
                        item["text"].contains("text") && item["text"]["text"].is_string()) {
                        note_item.text = item["text"]["text"].get<std::string>();
                    }
                    if (item.contains("checked") && item["checked"].is_boolean()) {
                        note_item.checked = item["checked"].get<bool>();
                    }
                    note.items.push_back(note_item);
                }
            }
        } else if (body.contains("text") && body["text"].is_object()) {
            note.is_list = false;
            const auto& text = body["text"];
            if (text.contains("text") && text["text"].is_string()) {
                note.text = text["text"].get<std::string>();
            }
        }
    }
    return note;
}

static json note_to_json(const KeepNote& note) {
    json j;
    if (!note.title.empty()) j["title"] = note.title;

    json body;
    if (note.is_list) {
        json list_items = json::array();
        for (const auto& item : note.items) {
            json ji;
            ji["text"] = json{{"text", item.text}};
            ji["checked"] = item.checked;
            list_items.push_back(ji);
        }
        body["list"] = json{{"listItems", list_items}};
    } else {
        body["text"] = json{{"text", note.text}};
    }
    j["body"] = body;
    return j;
}

GoogleKeepAPI::GoogleKeepAPI(const std::string& access_token) : access_token_(access_token) {}

void GoogleKeepAPI::set_access_token(const std::string& token) {
    access_token_ = token;
}

std::vector<KeepNote> GoogleKeepAPI::list_notes() {
    httplib::Client cli("https://keep.googleapis.com");
    cli.set_bearer_token_auth(access_token_);

    std::vector<KeepNote> notes;
    std::string page_token = "";

    do {
        std::string path = "/v1/notes?pageSize=100";
        if (!page_token.empty()) path += "&pageToken=" + page_token;

        auto res = cli.Get(path.c_str());
        if (res && res->status == 200) {
            json j = json::parse(res->body);
            if (j.contains("notes") && j["notes"].is_array()) {
                for (const auto& item : j["notes"]) {
                    notes.push_back(parse_note(item));
                }
            }
            if (j.contains("nextPageToken") && j["nextPageToken"].is_string()) {
                page_token = j["nextPageToken"].get<std::string>();
            } else {
                page_token = "";
            }
        } else {
            std::cerr << "Failed to fetch Keep notes. Status: " << (res ? std::to_string(res->status) : "null") << std::endl;
            if (res) std::cerr << "Body: " << res->body << std::endl;
            break;
        }
    } while (!page_token.empty());

    return notes;
}

std::optional<KeepNote> GoogleKeepAPI::create_note(const KeepNote& note) {
    httplib::Client cli("https://keep.googleapis.com");
    cli.set_bearer_token_auth(access_token_);

    json j = note_to_json(note);
    std::string body = j.dump();

    auto res = cli.Post("/v1/notes", body, "application/json");
    if (res && res->status == 200) {
        return parse_note(json::parse(res->body));
    }
    return std::nullopt;
}

bool GoogleKeepAPI::delete_note(const std::string& name) {
    httplib::Client cli("https://keep.googleapis.com");
    cli.set_bearer_token_auth(access_token_);

    auto res = cli.Delete("/v1/" + name);
    return res && res->status == 200;
}

} // namespace API
