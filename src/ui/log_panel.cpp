#include "log_panel.h"
#include "../core/log.h"
#include "imgui.h"
#include "IconsFontAwesome6.h"
#include <cfloat>
#include <cstdio>
#include <vector>

namespace ui::LogPanel {

namespace {

struct Toast {
    int id;
    core::Log::Level level;
    std::string message;
    double spawn_time;
};

std::vector<Toast> g_toasts;
int g_last_seen_id = -1;

constexpr double kToastLifetime = 6.0;   // seconds a toast stays up before auto-dismissing
constexpr double kToastFadeStart = 5.0;  // starts fading out this many seconds in
constexpr float kToastWidth = 340.0f;

ImVec4 LevelColor(core::Log::Level level) {
    switch (level) {
        case core::Log::Level::Warning: return ImVec4(0.95f, 0.75f, 0.25f, 1.0f);
        case core::Log::Level::Error: return ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
        default: return ImVec4(0.75f, 0.75f, 0.78f, 1.0f);
    }
}

const char* LevelIcon(core::Log::Level level) {
    switch (level) {
        case core::Log::Level::Warning: return ICON_FA_TRIANGLE_EXCLAMATION;
        case core::Log::Level::Error: return ICON_FA_CIRCLE_XMARK;
        default: return ICON_FA_CIRCLE_INFO;
    }
}

} // namespace

void RenderToasts() {
    // Pick up any Warning/Error entries logged since last frame - Info
    // entries are history-only (they'd make routine startup logging pop up
    // as alerts, defeating the point of a toast being something worth
    // interrupting the user for).
    const auto& history = core::Log::History();
    for (const auto& entry : history) {
        if (entry.id <= g_last_seen_id) continue;
        if (entry.level != core::Log::Level::Info) {
            g_toasts.push_back({ entry.id, entry.level, entry.message, ImGui::GetTime() });
        }
    }
    if (!history.empty()) g_last_seen_id = history.back().id;

    double now = ImGui::GetTime();
    for (size_t i = 0; i < g_toasts.size();) {
        if (now - g_toasts[i].spawn_time > kToastLifetime) {
            g_toasts.erase(g_toasts.begin() + static_cast<long>(i));
        } else {
            i++;
        }
    }
    if (g_toasts.empty()) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float margin = 16.0f;
    float y_offset = margin;

    // Newest toast anchored closest to the corner, older ones stacked
    // upward above it.
    for (auto it = g_toasts.rbegin(); it != g_toasts.rend(); ++it) {
        Toast& toast = *it;
        double age = now - toast.spawn_time;
        float alpha = age > kToastFadeStart
                          ? 1.0f - static_cast<float>((age - kToastFadeStart) / (kToastLifetime - kToastFadeStart))
                          : 1.0f;

        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin, viewport->WorkPos.y + viewport->WorkSize.y - y_offset),
            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(kToastWidth, 0), ImVec2(kToastWidth, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleColor(ImGuiCol_Border, LevelColor(toast.level));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);

        char window_id[32];
        snprintf(window_id, sizeof(window_id), "##toast%d", toast.id);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                  ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                  ImGuiWindowFlags_NoMove;
        ImGui::Begin(window_id, nullptr, flags);

        ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(toast.level));
        ImGui::TextUnformatted(LevelIcon(toast.level));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kToastWidth - 60.0f);
        ImGui::TextUnformatted(toast.message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::SameLine(ImGui::GetWindowWidth() - 28.0f);
        if (ImGui::SmallButton(ICON_FA_XMARK)) toast.spawn_time = -1e9; // next frame's age check removes it

        y_offset += ImGui::GetWindowHeight() + 8.0f;
        ImGui::End();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
}

void RenderWindow(bool* open) {
    if (!*open) return;
    ImGui::SetNextWindowSize(ImVec2(560, 320), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Log", open)) {
        ImGui::End();
        return;
    }

    static bool auto_scroll = true;
    if (ImGui::SmallButton("Clear")) core::Log::Clear();
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) {
        ImGui::LogToClipboard();
        for (const auto& entry : core::Log::History()) ImGui::LogText("%s\n", entry.message.c_str());
        ImGui::LogFinish();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &auto_scroll);

    ImGui::Separator();
    ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& entry : core::Log::History()) {
        ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(entry.level));
        ImGui::TextUnformatted(LevelIcon(entry.level));
        ImGui::SameLine();
        ImGui::TextUnformatted(entry.message.c_str());
        ImGui::PopStyleColor();
    }
    if (auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::End();
}

} // namespace ui::LogPanel
