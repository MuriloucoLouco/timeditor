#pragma once
#include <cstdint>
#include <vector>
#include <string>

// Fixed 8-byte header at the start of every TIM block: signature + pixel/CLUT flags.
struct TIM_Header {
    uint32_t id;   // Always 0x00000010
    uint32_t flag; // bits 0-2: pixel mode, bit 3: has CLUT
};

// Palette (CLUT) header, present only when TIM_Header::flag's bit 3 is set.
struct TIM_CLUT_Header {
    uint32_t size;
    uint16_t origin_x;       // VRAM X (words) - Palette Org
    uint16_t origin_y;       // VRAM Y (pixels) - Palette Org
    uint16_t colors_per_clut;
    uint16_t num_cluts;
};

// Header for the raw pixel data block.
struct TIM_Image_Header {
    uint32_t size;
    uint16_t origin_x; // VRAM X (words) - Image Org
    uint16_t origin_y; // VRAM Y (pixels) - Image Org
    uint16_t width;    // In 16-bit words, not pixels
    uint16_t height;
};

// PS1 pixel formats (TIM_Header::flag & 0x07).
enum class TIMPixelMode : uint8_t {
    Indexed4BPP = 0,
    Indexed8BPP = 1,
    Direct16BPP = 2,
    Direct24BPP = 3,
    Mixed = 4,
};

// A fully loaded and decoded TIM image, including data ready to become a
// texture (opengl_texture_ids is filled by gfx::TIMTextureBuilder, not the parser).
class TIM_Image {
public:
    std::string filename;
    int file_index = 0; // Position of this image within its source file (a file can hold several)

    TIM_Header header{};
    bool has_clut = false;
    uint8_t type = 0; // See TIMPixelMode

    TIM_CLUT_Header clut_header{};
    std::vector<uint16_t> clut_data; // BGR555, size = colors_per_clut * num_cluts

    TIM_Image_Header image_header{};
    std::vector<uint8_t> image_data; // Raw bytes, still in the packed PS1 layout

    int real_width = 0; // Pixel width derived from image_header.width
    int bpp = 0;         // 4, 8, 16 or 24

    // Filled by gfx::TIMTextureBuilder: one OpenGL texture per CLUT
    // (or a single texture for direct-color formats).
    std::vector<uint32_t> opengl_texture_ids;
    int selected_clut = 0;

    // Shared multi-select flag: the Inspector's per-file checkboxes and the
    // VRAM Viewer's image selection (click/Ctrl/Shift) both read and write
    // this, so selecting in one panel is reflected in the other.
    bool selected = false;

    // True if this specific image (its own origin, pixels, or CLUT) has
    // unsaved changes. Document::IsFileDirty(filepath) is just "does any
    // image in this file have dirty=true" - this is the one place that
    // actually gets set/cleared, so the two can never disagree.
    bool dirty = false;

    // --- Image Editor state ---
    //
    // "Highest quality" master copy: RGBA8888, master_width x
    // image_header.height. Every BPP conversion (image_data/clut_data) is
    // re-derived from this, so repeatedly switching color depths never
    // compounds quality loss - only master_rgba itself is ever painted on
    // directly. Empty until gfx::TIMTextureBuilder::EnsureMasterImage has
    // bootstrapped it (lazily, the first time this image is opened in the
    // Image Editor) or ImportImageDialog has replaced it outright.
    std::vector<uint8_t> master_rgba;
    int master_width = 0;

    // Parallel to master_rgba (one entry per master pixel): which palette
    // index currently produced that pixel's color. Only meaningful while
    // bpp is 4 or 8 (indexed); populated whenever master_rgba is
    // quantized into image_data/clut_data. Lets editing one palette color
    // repaint every master pixel using that index without a full requantize.
    std::vector<uint8_t> master_index_map;
};
