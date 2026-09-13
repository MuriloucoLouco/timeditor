#include "editor_app.h"
#include "theme.h"
#include "imgui.h"
#include "../core/tim_parser.h"
#include "../gfx/tim_texture_builder.h"
#include "portable-file-dialogs.h"
#include <set>

namespace ui {

void EditorApp::Initialize() {
    vram_manager.InitializeGL();
    ApplyDarkTheme();
}

void EditorApp::LoadFile(const std::string& path) {
    // Already open - skip re-loading it as a duplicate set of images (this
    // matters more now that files can also arrive via drag-and-drop or
    // command-line args, where accidentally listing the same path twice is easy).
    for (const auto& existing : document.Images()) {
        if (existing.filename == path) return;
    }

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

void EditorApp::RequestExit() {
    bool any_dirty = false;
    for (const auto& tim : document.Images()) {
        if (document.IsFileDirty(tim.filename)) { any_dirty = true; break; }
    }

    if (any_dirty) {
        open_exit_confirm = true;
    } else {
        should_quit = true;
    }
}

void EditorApp::RenderExitConfirmPopup() {
    if (open_exit_confirm) {
        ImGui::OpenPopup("Quit TIMEditor?");
        open_exit_confirm = false;
    }

    if (ImGui::BeginPopupModal("Quit TIMEditor?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("You have unsaved changes.");
        ImGui::Text("Save all changes before quitting?");
        ImGui::Separator();

        if (ImGui::Button("Save All & Quit")) {
            std::set<std::string> saved;
            for (const auto& tim : document.Images()) {
                if (saved.insert(tim.filename).second && document.IsFileDirty(tim.filename)) {
                    document.Save(tim.filename);
                }
            }
            should_quit = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard & Quit")) {
            should_quit = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorApp::HandleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        SaveActiveFile();
    }
    // Skip while a text field (e.g. the export dialog's path box) is
    // focused, so this doesn't fight with that widget's own Ctrl+Z.
    if (io.KeyCtrl && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        PerformUndo();
    }
}

void EditorApp::PerformUndo() {
    int rebuild_index = -1;
    if (document.Undo(rebuild_index) && rebuild_index >= 0) {
        // A content edit (paint/BPP switch/resize/import) was undone -
        // dimensions or CLUT count may have changed, so its GL textures
        // need rebuilding (a move/delete undo never touches those).
        gfx::TIMTextureBuilder::RebuildTextures(document.Images()[rebuild_index]);
    }
}

void EditorApp::RenderFrame() {
    HandleShortcuts();
    RenderMenu();
    export_dialog.Render(document);
    RenderExitConfirmPopup();
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
            if (ImGui::MenuItem("Exit")) RequestExit();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, document.CanUndo())) PerformUndo();
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

        // The Inspector's "Go to VRAM" buttons (just rendered above, if
        // clicked this frame) request selecting and focusing an image/CLUT
        // in the VRAM Viewer and force-switching to that tab to show it.
        int focus_index;
        bool focus_is_clut;
        if (inspector_panel.ConsumePendingVramFocus(focus_index, focus_is_clut)) {
            vram_panel.FocusOn(document, focus_index, focus_is_clut);
            switch_to_vram_tab = true;
        }

        ImGuiTabItemFlags vram_tab_flags = switch_to_vram_tab ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("VRAM Viewer", nullptr, vram_tab_flags)) {
            vram_panel.Render(document, vram_manager);
            ImGui::EndTabItem();
        }
        switch_to_vram_tab = false;
        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace ui
