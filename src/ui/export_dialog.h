#pragma once
#include "../core/tim_document.h"
#include <string>

namespace ui {

// Modal popup for "Export Selected Images...": pick a format (BMP/PNG/JPG),
// a destination folder, and (for JPG) a quality level, then write every
// currently-selected TIM_Image out as a standalone raster file.
class ExportDialog {
public:
    // Call when the menu item is clicked; the popup actually opens on the
    // next Render() call.
    void Open();

    // Must be called every frame (the popup only becomes visible after Open()).
    void Render(tim::Document& document);

private:
    bool should_open = false;
    int format_index = 1; // 0 = BMP, 1 = PNG, 2 = JPG
    int jpg_quality = 90;
    char output_dir[512] = "";
    std::string status_message;
};

} // namespace ui
