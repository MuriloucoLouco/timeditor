#pragma once
#include "../core/tim_document.h"
#include <string>

namespace ui {

// "TIM Inspector" tab: file list + zoomed preview of the selected TIM,
// including its CLUT selector and palette swatches.
//
// The file list groups images by their source file: each loaded .tim is a
// parent row (with its own checkbox to select/deselect all of its images at
// once) and its images are shown indented underneath as children.
class InspectorPanel {
public:
    void Render(tim::Document& document);

private:
    float zoom_level = 2.0f;

    // Set when the user asks to close a dirty file; drives the "save before
    // closing?" modal, which is opened/drawn on the next RenderFileList call.
    std::string pending_close_file;
    bool open_close_confirm = false;

    void RenderFileList(tim::Document& document);
    void RenderCloseConfirmPopup(tim::Document& document);
    void RenderPreview(tim::Document& document, TIM_Image& tim);
    void RenderClutSelector(TIM_Image& tim);
};

} // namespace ui
