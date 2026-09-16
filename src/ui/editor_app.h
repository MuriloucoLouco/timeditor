#pragma once
#include <string>
#include "../core/tim_document.h"
#include "../core/vram_manager.h"
#include "inspector_panel.h"
#include "vram_panel.h"
#include "tmd_panel.h"
#include "export_dialog.h"

namespace ui {

// Owns the document (loaded TIMs) and VRAM state, handles the main menu,
// and delegates tab rendering to InspectorPanel/VRAMPanel.
class EditorApp {
public:
    EditorApp() = default;

    void Initialize();
    void RenderFrame();
    void LoadFile(const std::string& path);

    // Called from File > Exit and from the OS window's close button. If
    // anything is unsaved, opens a confirmation dialog instead of quitting
    // right away.
    void RequestExit();
    bool ShouldQuit() const { return should_quit; }

private:
    // Which top-level workspace tab is currently open, set each frame from
    // inside RenderWorkspace()'s tab-item blocks - used to route Ctrl+Z/
    // Ctrl+S and the Edit/File menu's enabled state to whichever of
    // Document/TmdPanel the user is actually looking at.
    enum class WorkspaceTab { TimInspector, VramViewer, TmdViewer };

    tim::Document document;
    VRAMManager vram_manager;
    InspectorPanel inspector_panel;
    VRAMPanel vram_panel;
    TmdPanel tmd_panel;
    ExportDialog export_dialog;
    bool should_quit = false;
    bool open_exit_confirm = false;
    bool switch_to_vram_tab = false;
    bool switch_to_tim_tab = false;
    bool show_log_window = false;
    WorkspaceTab active_tab = WorkspaceTab::TimInspector;

    void RenderMenu();
    void RenderWorkspace();
    void RenderExitConfirmPopup();
    void HandleShortcuts();
    void SaveActiveFile();
    void SaveActiveFileAs();
    void SaveActiveTmd();
    void SaveActiveTmdAs();
    void PerformUndo();
    void PerformRedo();

    // File > New TIM Image...: prompts for a save path up front (there's
    // nothing to group a brand-new file's images under otherwise - see
    // Document::AddBlankImage's filepath parameter) and adds one blank
    // 32x32 16bpp image to it, same as the Inspector's own "+ Add New
    // Image" button does for a file that's already open.
    void NewTim();
};

} // namespace ui
