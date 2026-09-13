#pragma once
#include "../core/tim_document.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ui {

// Modal popup for "Import Image...": pick a source image file (any common
// format, decoded via stb_image), choose a BPP mode with a live quantized
// preview + palette, and Apply to replace the target image's content
// (and its master edit buffer) wholesale.
class ImportImageDialog {
public:
    // Call when "Import Image..." is clicked; the popup opens on the next Render().
    void Open(int target_index);

    // Must be called every frame from the owning ImageEditorPanel.
    void Render(tim::Document& document);

private:
    bool should_open = false;
    int target_index = -1;

    std::vector<uint8_t> source_rgba; // decoded, RGBA8888
    int source_width = 0, source_height = 0;
    std::string source_path;
    std::string status_message;

    int bpp_index = 2; // 0=4bpp, 1=8bpp, 2=16bpp, 3=24bpp

    // The canvas size to actually import at (defaults to the source's own
    // size). fit_mode true stretch-resizes the source to this size; false
    // crops/pads it instead, anchored top-left (extra space stays transparent).
    int import_width = 0, import_height = 0;
    bool fit_mode = false;

    uint32_t preview_texture = 0;
    std::vector<uint16_t> preview_palette;
    bool preview_dirty = true;

    void PickSourceFile();
    // Returns the source resampled/cropped to import_width x import_height.
    std::vector<uint8_t> BuildImportBuffer() const;
    void RegeneratePreview();
    void ApplyToTarget(tim::Document& document);
};

} // namespace ui
