#pragma once

#include <string>
#include <vector>
#include <optional>

namespace API {

struct KeepNoteItem {
    std::string text;
    bool checked = false;
};

struct KeepNote {
    std::string name;  // "notes/{id}", empty until created
    std::string title;
    std::string text;               // used when is_list == false
    std::vector<KeepNoteItem> items; // used when is_list == true
    bool is_list = false;
    std::string create_time;
    std::string update_time;
    bool trashed = false;
};

// The Keep API (notes.create/get/list/delete) has never supported editing a
// note's content, so there is intentionally no update_note here — "editing" a
// note in this app means delete_note() + create_note() with a new id.
class GoogleKeepAPI {
public:
    GoogleKeepAPI(const std::string& access_token);

    void set_access_token(const std::string& token);

    std::vector<KeepNote> list_notes();
    std::optional<KeepNote> create_note(const KeepNote& note);

    // Permanent: the Keep API has no trash/undo for notes.delete.
    bool delete_note(const std::string& name);

private:
    std::string access_token_;
};

} // namespace API
