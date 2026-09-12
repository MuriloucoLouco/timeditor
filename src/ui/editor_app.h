#pragma once
#include <string>
#include "../core/tim_document.h"
#include "../core/vram_manager.h"
#include "inspector_panel.h"
#include "vram_panel.h"
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
    tim::Document document;
    VRAMManager vram_manager;
    InspectorPanel inspector_panel;
    VRAMPanel vram_panel;
    ExportDialog export_dialog;
    bool should_quit = false;
    bool open_exit_confirm = false;

    void RenderMenu();
    void RenderWorkspace();
    void RenderExitConfirmPopup();
    void HandleShortcuts();
    void SaveActiveFile();
    void SaveActiveFileAs();
};

} // namespace ui
