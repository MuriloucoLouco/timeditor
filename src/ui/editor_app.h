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

private:
    tim::Document document;
    VRAMManager vram_manager;
    InspectorPanel inspector_panel;
    VRAMPanel vram_panel;
    ExportDialog export_dialog;

    void RenderMenu();
    void RenderWorkspace();
    void HandleShortcuts();
    void SaveActiveFile();
    void SaveActiveFileAs();
};

} // namespace ui
