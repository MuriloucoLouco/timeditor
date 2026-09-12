#include "editor_app.h"
#include "theme.h"
#include "imgui.h"
#include "../core/tim_parser.h"
#include "../gfx/tim_texture_builder.h"
#include "portable-file-dialogs.h"

namespace ui {

void EditorApp::Initialize() {
    vram_manager.InitializeGL();
    ApplyDarkTheme();
}

void EditorApp::LoadFile(const std::string& path) {
    std::vector<TIM_Image> new_tims;
    if (!tim::Parser::LoadFromFile(path, new_tims)) return;

    for (auto& new_tim : new_tims) {
        gfx::TIMTextureBuilder::BuildTextures(new_tim);
    }

    int first_index = document.AddImages(std::move(new_tims));
    if (first_index != -1 && document.GetActiveIndex() == -1) {
        document.SetActiveIndex(first_index);
    }
}

void EditorApp::SaveActiveFile() {
    int idx = document.GetActiveIndex();
    if (idx < 0) return;
    document.Save(document.Images()[idx].filename);
}

void EditorApp::SaveActiveFileAs() {
    int idx = document.GetActiveIndex();
    if (idx < 0 || !pfd::settings::available()) return;

    std::string source = document.Images()[idx].filename;
    std::string dest = pfd::save_file("Save TIM as", source, { "TIM Files (.tim)", "*.tim" }).result();
    if (dest.empty()) return;

    document.SaveAs(source, dest);
}

void EditorApp::HandleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        SaveActiveFile();
    }
}

void EditorApp::RenderFrame() {
    HandleShortcuts();
    RenderMenu();
    export_dialog.Render(document);
    RenderWorkspace();
}

void EditorApp::RenderMenu() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open TIM...")) {
                if (pfd::settings::available()) {
                    auto selection = pfd::open_file("Select TIM files", ".",
                                                     { "TIM Files (.tim)", "*.tim", "All Files", "*" },
                                                     pfd::opt::multiselect).result();
                    for (const auto& path : selection) {
                        LoadFile(path);
                    }
                }
            }
            ImGui::Separator();

            bool has_active = document.GetActiveIndex() != -1;
            if (ImGui::MenuItem("Save", "Ctrl+S", false, has_active)) SaveActiveFile();
            if (ImGui::MenuItem("Save As...", nullptr, false, has_active)) SaveActiveFileAs();

            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) exit(0);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Export")) {
            bool has_selection = false;
            for (const auto& tim : document.Images()) {
                if (tim.selected) { has_selection = true; break; }
            }
            if (ImGui::MenuItem("Export Selected Images...", nullptr, false, has_selection)) {
                export_dialog.Open();
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void EditorApp::RenderWorkspace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + 20));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - 20));

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("WorkspaceAbas", nullptr, window_flags);

    if (ImGui::BeginTabBar("AbasPrincipais", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("TIM Inspector")) {
            inspector_panel.Render(document);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("VRAM Viewer")) {
            vram_panel.Render(document, vram_manager);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace ui
