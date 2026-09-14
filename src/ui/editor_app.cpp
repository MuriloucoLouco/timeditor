#include "editor_app.h"
#include "theme.h"
#include "imgui.h"
#include "../core/tim_parser.h"
#include "../gfx/tim_texture_builder.h"
#include "../gfx/gl_ext.h"
#include "file_dialog.h"
#include <cctype>
#include <set>

namespace ui {

void EditorApp::Initialize() {
    vram_manager.InitializeGL();
    gfx::gl::LoadGLExtensions();
    ApplyDarkTheme();
}

void EditorApp::LoadFile(const std::string& path) {
    // Drag-and-drop and command-line args can hand either file type to the
    // same entry point - dispatch by extension so both "just work" there.
    if (path.size() >= 4) {
        std::string ext = path.substr(path.size() - 4);
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".tmd") {
            tmd_panel.LoadFile(path, document);
            return;
        }
    }

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

void EditorApp::NewTim() {
    std::string path = FileDialog::SaveFile("New TIM file", { { "TIM Files", "tim" } });
    if (path.empty()) return;

    // Same de-dupe rule as LoadFile - a filename already in the document
    // just gets selected instead of quietly creating a second same-named
    // group in the sidebar.
    const auto& images = document.Images();
    for (size_t i = 0; i < images.size(); i++) {
        if (images[i].filename == path) {
            document.SetActiveIndex(static_cast<int>(i));
            inspector_panel.FocusImage();
            switch_to_tim_tab = true;
            return;
        }
    }

    int idx = document.AddBlankImage(path);
    if (idx < 0) return;
    gfx::TIMTextureBuilder::BuildTextures(document.Images()[idx]);
    document.SetActiveIndex(idx);
    inspector_panel.FocusImage();
    switch_to_tim_tab = true;
}

void EditorApp::SaveActiveFileAs() {
    int idx = document.GetActiveIndex();
    if (idx < 0) return;

    std::string source = document.Images()[idx].filename;
    std::string dest = FileDialog::SaveFile("Save TIM as", { { "TIM Files", "tim" } }, source);
    if (dest.empty()) return;

    document.SaveAs(source, dest);
}

void EditorApp::RequestExit() {
    bool any_dirty = tmd_panel.AnyModelDirty();
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
            tmd_panel.SaveAllDirtyModels();
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
        if (active_tab == WorkspaceTab::TmdViewer) SaveActiveTmd();
        else SaveActiveFile();
    }
    // Skip while a text field (e.g. the export dialog's path box) is
    // focused, so this doesn't fight with that widget's own Ctrl+Z/Ctrl+Y-
    // style text undo/redo.
    if (io.KeyCtrl && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        if (io.KeyShift) PerformRedo();
        else PerformUndo();
    }
}

void EditorApp::PerformUndo() {
    if (active_tab == WorkspaceTab::TmdViewer) {
        tmd_panel.Undo();
        return;
    }

    int rebuild_index = -1;
    if (document.Undo(rebuild_index) && rebuild_index >= 0) {
        // A content edit (paint/BPP switch/resize/import) was undone -
        // dimensions or CLUT count may have changed, so its GL textures
        // need rebuilding (a move/delete undo never touches those).
        gfx::TIMTextureBuilder::RebuildTextures(document.Images()[rebuild_index]);
    }
}

void EditorApp::PerformRedo() {
    if (active_tab == WorkspaceTab::TmdViewer) {
        tmd_panel.Redo();
        return;
    }

    int rebuild_index = -1;
    if (document.Redo(rebuild_index) && rebuild_index >= 0) {
        gfx::TIMTextureBuilder::RebuildTextures(document.Images()[rebuild_index]);
    }
}

void EditorApp::SaveActiveTmd() { tmd_panel.SaveActiveModel(); }

void EditorApp::SaveActiveTmdAs() {
    if (!tmd_panel.HasActiveModel()) return;
    std::string dest = FileDialog::SaveFile("Save TMD as", { { "TMD Files", "tmd" } }, tmd_panel.ActiveModelFilename());
    if (dest.empty()) return;
    tmd_panel.SaveActiveModelAs(dest);
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
            // New/Open grouped into their own submenus (rather than four
            // top-level items interleaved by file type) so the menu reads
            // as "what do I want to do" first, "which file type" second.
            if (ImGui::BeginMenu("New")) {
                if (ImGui::MenuItem("TIM Image...")) NewTim();
                if (ImGui::MenuItem("TMD Model...")) tmd_panel.NewModel();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Open")) {
                if (ImGui::MenuItem("TIM...")) {
                    auto selection = FileDialog::OpenFiles("Select TIM files", { { "TIM Files", "tim" } });
                    for (const auto& path : selection) {
                        LoadFile(path);
                    }
                }
                if (ImGui::MenuItem("TMD...")) {
                    auto selection = FileDialog::OpenFiles("Select TMD files", { { "TMD Files", "tmd" } });
                    for (const auto& path : selection) {
                        tmd_panel.LoadFile(path, document);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();

            bool has_active = document.GetActiveIndex() != -1;
            if (ImGui::MenuItem("Save TIM", "Ctrl+S", false, has_active)) SaveActiveFile();
            if (ImGui::MenuItem("Save TIM As...", nullptr, false, has_active)) SaveActiveFileAs();
            if (ImGui::MenuItem("Save TMD", nullptr, false, tmd_panel.HasActiveModel())) SaveActiveTmd();
            if (ImGui::MenuItem("Save TMD As...", nullptr, false, tmd_panel.HasActiveModel())) SaveActiveTmdAs();

            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) RequestExit();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            bool can_undo = active_tab == WorkspaceTab::TmdViewer ? tmd_panel.CanUndo() : document.CanUndo();
            bool can_redo = active_tab == WorkspaceTab::TmdViewer ? tmd_panel.CanRedo() : document.CanRedo();
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, can_undo)) PerformUndo();
            if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, can_redo)) PerformRedo();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Import")) {
            if (ImGui::MenuItem("Import Model...", nullptr, false, tmd_panel.HasActiveModel())) {
                tmd_panel.OpenImportDialog();
            }
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
            ImGui::Separator();
            if (ImGui::MenuItem("Export Model...", nullptr, false, tmd_panel.HasActiveModel())) {
                tmd_panel.OpenExportDialog();
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
        ImGuiTabItemFlags tim_tab_flags = switch_to_tim_tab ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("TIM Editor", nullptr, tim_tab_flags)) {
            active_tab = WorkspaceTab::TimInspector;
            inspector_panel.Render(document);
            ImGui::EndTabItem();
        }
        switch_to_tim_tab = false;

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
            active_tab = WorkspaceTab::VramViewer;
            vram_panel.Render(document, vram_manager);
            ImGui::EndTabItem();
        }
        switch_to_vram_tab = false;

        if (ImGui::BeginTabItem("TMD Editor")) {
            active_tab = WorkspaceTab::TmdViewer;
            tmd_panel.Render(document, vram_manager);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace ui
