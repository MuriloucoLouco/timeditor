#include "export_dialog.h"
#include "imgui.h"
#include "../gfx/image_exporter.h"
#include "portable-file-dialogs.h"
#include <cstring>
#include <set>

namespace ui {

namespace {

std::string BaseNameNoExt(const std::string& path) {
    std::string name = path.substr(path.find_last_of("/\\") + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return name;
}

gfx::ExportFormat FormatFromIndex(int index) {
    switch (index) {
        case 0: return gfx::ExportFormat::BMP;
        case 2: return gfx::ExportFormat::JPG;
        default: return gfx::ExportFormat::PNG;
    }
}

int CountSelected(const tim::Document& document) {
    int count = 0;
    for (const auto& tim : document.Images()) {
        if (tim.selected) count++;
    }
    return count;
}

} // namespace

void ExportDialog::Open() {
    should_open = true;
    status_message.clear();
}

void ExportDialog::Render(tim::Document& document) {
    if (should_open) {
        ImGui::OpenPopup("Export Selected Images");
        should_open = false;
    }

    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal("Export Selected Images", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    int selected_count = CountSelected(document);
    ImGui::Text("%d image(s) selected for export.", selected_count);
    if (selected_count == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Select at least one image first.");
    }
    ImGui::Separator();

    const char* formats[] = { "BMP", "PNG", "JPG" };
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("Format", &format_index, formats, 3);

    if (format_index == 2) {
        ImGui::SliderInt("JPEG Quality", &jpg_quality, 1, 100);
        ImGui::TextDisabled("JPEG has no transparency; PS1 color-key alpha is flattened.");
    }

    ImGui::Spacing();
    ImGui::InputText("##outdir", output_dir, sizeof(output_dir));
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        if (pfd::settings::available()) {
            std::string dir = pfd::select_folder("Choose export folder", output_dir).result();
            if (!dir.empty()) {
                std::strncpy(output_dir, dir.c_str(), sizeof(output_dir) - 1);
                output_dir[sizeof(output_dir) - 1] = '\0';
            }
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Output folder");

    ImGui::Separator();
    if (!status_message.empty()) {
        ImGui::TextWrapped("%s", status_message.c_str());
        ImGui::Separator();
    }

    bool can_export = selected_count > 0 && output_dir[0] != '\0';
    ImGui::BeginDisabled(!can_export);
    if (ImGui::Button("Export")) {
        gfx::ExportFormat format = FormatFromIndex(format_index);
        const char* ext = gfx::ImageExporter::ExtensionFor(format);
        std::string dir(output_dir);
        if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';

        std::set<std::string> used_names;
        int ok = 0, failed = 0;
        for (auto& tim : document.Images()) {
            if (!tim.selected) continue;

            std::string base = BaseNameNoExt(tim.filename);
            std::string candidate = base;
            int suffix = 1;
            while (used_names.count(candidate)) {
                candidate = base + "_" + std::to_string(++suffix);
            }
            used_names.insert(candidate);

            std::string path = dir + candidate + ext;
            if (gfx::ImageExporter::Export(tim, path, format, jpg_quality)) ok++;
            else failed++;
        }

        status_message = std::to_string(ok) + " image(s) exported to " + dir;
        if (failed > 0) status_message += " (" + std::to_string(failed) + " failed)";
        if (failed == 0) ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        status_message.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

} // namespace ui
