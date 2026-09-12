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
        loaded_tims.push_back(std::move(new_tim));
    }
}

void EditorApp::RenderFrame() {
    RenderMenu();
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
            if (ImGui::MenuItem("Exit")) {
                exit(0);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Selection")) {
            if (ImGui::MenuItem("Draw selected TIMs to VRAM")) {
                for (auto& tim : loaded_tims) {
                    if (tim.selected) {
                        vram_manager.WriteTIMToVRAM(tim);
                    }
                }
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
            inspector_panel.Render(loaded_tims);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("VRAM Viewer")) {
            vram_panel.Render(vram_manager);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace ui
