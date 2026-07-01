#pragma once

#include "imgui.h"
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace ImGui {

inline bool DatePicker(const char* label, std::string& date_str) {
    bool changed = false;
    
    // Parse current date
    int year = 2026, month = 1, day = 1;
    if (date_str.length() >= 10) {
        year = std::stoi(date_str.substr(0, 4));
        month = std::stoi(date_str.substr(5, 2));
        day = std::stoi(date_str.substr(8, 2));
    } else {
        time_t t = time(nullptr);
        struct tm* tm_now = localtime(&t);
        year = tm_now->tm_year + 1900;
        month = tm_now->tm_mon + 1;
        day = tm_now->tm_mday;
    }
    
    ImGui::PushID(label);
    
    std::string display_str = date_str.empty() ? "Select Date" : date_str;
    if (ImGui::Button(display_str.c_str(), ImVec2(120, 0))) {
        ImGui::OpenPopup("DatePickerPopup");
        ImGui::GetStateStorage()->SetInt(ImGui::GetID("view_year"), year);
        ImGui::GetStateStorage()->SetInt(ImGui::GetID("view_month"), month);
    }
    ImGui::SameLine();
    ImGui::Text("%s", label);
    
    if (ImGui::BeginPopup("DatePickerPopup")) {
        int view_year = ImGui::GetStateStorage()->GetInt(ImGui::GetID("view_year"), year);
        int view_month = ImGui::GetStateStorage()->GetInt(ImGui::GetID("view_month"), month);
        
        if (ImGui::Button("<")) {
            view_month--;
            if (view_month < 1) { view_month = 12; view_year--; }
            ImGui::GetStateStorage()->SetInt(ImGui::GetID("view_year"), view_year);
            ImGui::GetStateStorage()->SetInt(ImGui::GetID("view_month"), view_month);
        }
        ImGui::SameLine();
        
        char month_year[64];
        const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        snprintf(month_year, sizeof(month_year), "%s %04d", months[view_month - 1], view_year);
        
        float text_width = ImGui::CalcTextSize(month_year).x;
        float expected_width = 240.0f;
        float spacing = (expected_width - text_width - ImGui::CalcTextSize("<").x - ImGui::CalcTextSize(">").x - ImGui::GetStyle().ItemSpacing.x * 4) / 2.0f;
        if (spacing < 0) spacing = 0;
        
        ImGui::SameLine(ImGui::GetCursorPosX() + spacing);
        ImGui::Text("%s", month_year);
        
        ImGui::SameLine(expected_width - ImGui::CalcTextSize(">").x - ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button(">")) {
            view_month++;
            if (view_month > 12) { view_month = 1; view_year++; }
            ImGui::GetStateStorage()->SetInt(ImGui::GetID("view_year"), view_year);
            ImGui::GetStateStorage()->SetInt(ImGui::GetID("view_month"), view_month);
        }
        
        ImGui::Separator();
        
        if (ImGui::BeginTable("Days", 7)) {
            const char* days[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
            for (int i = 0; i < 7; i++) {
                ImGui::TableSetupColumn(days[i], ImGuiTableColumnFlags_WidthFixed, 25.0f);
            }
            ImGui::TableHeadersRow();
            
            struct tm t = {0};
            t.tm_year = view_year - 1900;
            t.tm_mon = view_month - 1;
            t.tm_mday = 1;
            mktime(&t);
            int start_dow = t.tm_wday; 
            
            int max_days = 31;
            if (view_month == 4 || view_month == 6 || view_month == 9 || view_month == 11) max_days = 30;
            else if (view_month == 2) {
                bool is_leap = (view_year % 4 == 0 && view_year % 100 != 0) || (view_year % 400 == 0);
                max_days = is_leap ? 29 : 28;
            }
            
            int current_day = 1;
            for (int row = 0; row < 6; row++) {
                ImGui::TableNextRow();
                for (int col = 0; col < 7; col++) {
                    ImGui::TableNextColumn();
                    if (row == 0 && col < start_dow) {
                        // Empty
                    } else if (current_day <= max_days) {
                        char day_buf[16];
                        snprintf(day_buf, sizeof(day_buf), "%d", current_day);
                        
                        bool is_selected = (view_year == year && view_month == month && current_day == day);
                        if (is_selected) {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                        }
                        
                        if (ImGui::Button(day_buf, ImVec2(25, 0))) {
                            char buf[32];
                            snprintf(buf, sizeof(buf), "%04d-%02d-%02d", view_year, view_month, current_day);
                            date_str = buf;
                            changed = true;
                            ImGui::CloseCurrentPopup();
                        }
                        
                        if (is_selected) {
                            ImGui::PopStyleColor();
                        }
                        
                        current_day++;
                    }
                }
                if (current_day > max_days) break;
            }
            ImGui::EndTable();
            
            ImGui::Separator();
            if (ImGui::Button("Clear", ImVec2(-FLT_MIN, 0))) {
                date_str = "";
                changed = true;
                ImGui::CloseCurrentPopup();
            }
        }
        
        ImGui::EndPopup();
    }
    
    ImGui::PopID();
    return changed;
}

} // namespace ImGui
