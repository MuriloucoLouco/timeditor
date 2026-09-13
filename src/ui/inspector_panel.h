#pragma once
#include "../core/tim_document.h"
#include "image_editor_panel.h"
#include <string>

namespace ui {

// "TIM Inspector" tab: file list on the left; on the right, one of a file
// overview page, or a per-image view split into "Info" (properties, VRAM
// navigation, delete) and "Image Editor" (paint/import/palette editing).
//
// The file list groups images by their source file: each loaded .tim is a
// parent row (with its own checkbox to select/deselect all of its images at
// once) and its images are shown indented underneath as children. Clicking
// the file's name (not its checkbox or disclosure arrow) opens its overview
// page; clicking an image row opens that image's view.
class InspectorPanel {
public:
    void Render(tim::Document& document);

    // If a "Go to VRAM" button (Info tab) was clicked this frame, moves the
    // requested image/CLUT index into the out-params and returns true -
    // EditorApp forwards it into VRAMPanel::FocusOn (which also selects it)
    // and switches tabs.
    bool ConsumePendingVramFocus(int& index, bool& is_clut);

private:
    static constexpr float kListMinWidth = 150.0f;
    static constexpr float kListMaxWidth = 500.0f;

    enum class ViewMode { Empty, Image, FileOverview };
    ViewMode view_mode = ViewMode::Empty;
    std::string overview_file;

    struct PendingVramFocus {
        bool pending = false;
        int index = -1;
        bool is_clut = false;
    } pending_vram_focus;

    float zoom_level = 2.0f;
    float list_width = 240.0f; // User-adjustable via the splitter next to it.

    // Set when the user asks to close a dirty file; drives the "save before
    // closing?" modal, which is opened/drawn on the next RenderFileList call.
    std::string pending_close_file;
    bool open_close_confirm = false;

    ImageEditorPanel image_editor;

    void RenderFileList(tim::Document& document);
    void RenderCloseConfirmPopup(tim::Document& document);
    void RenderFileOverview(tim::Document& document, const std::string& filepath);
    void RenderPreview(tim::Document& document, TIM_Image& tim); // "Info" tab body
    void RenderClutSelector(TIM_Image& tim);
};

} // namespace ui
