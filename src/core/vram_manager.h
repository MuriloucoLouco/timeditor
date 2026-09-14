#pragma once
#include "tim_format.h"
#include <vector>
#include <cstdint>

// Display-only interpretation of VRAM content. The underlying data never
// changes; this only controls how each 16-bit word is decoded for viewing.
enum class VRAMViewMode : uint8_t {
    Indexed4BPP, // Each word -> 4 grayscale pixels (nibbles)
    Indexed8BPP, // Each word -> 2 grayscale pixels (bytes)
    Direct16BPP, // Each word -> 1 BGR555 pixel (native direct-color mode)
};

// A VRAM coordinate expressed as a PS1 tpage index plus the position inside
// that tpage, as produced by VRAMManager::ComputeTPageLocation.
struct TPageLocation {
    int tpage_id;
    int local_x_words; // 0-63 within the tpage
    int local_y;        // 0-255 within the tpage
};

// Emulates the PS1's 1024x512, 16-bit VRAM and keeps an OpenGL texture
// mirroring its content for display.
class VRAMManager {
public:
    static constexpr int kWidth = 1024; // VRAM width in 16-bit words
    static constexpr int kHeight = 512;
    static constexpr int kTPageWidthWords = 64;
    static constexpr int kTPageHeight = 256;
    static constexpr int kTPagesPerRow = kWidth / kTPageWidthWords; // 16

    // Converts a VRAM coordinate (words on X, lines/pixels on Y) into a PS1
    // tpage index and the position inside it. Shared by the Inspector (Image
    // Org / Palette Org) and the VRAM Viewer (mouse cursor).
    static TPageLocation ComputeTPageLocation(int x_words, int y);

    VRAMManager();
    ~VRAMManager();

    void InitializeGL();

    // Clears the emulated VRAM and re-writes every image's CLUT/pixels at its
    // current Image/Palette Org, then refreshes the display texture. Doing a
    // full rebuild (instead of writing just the moved image) is what keeps a
    // moved image from leaving a stale copy behind at its old position.
    void RebuildFromImages(const std::vector<TIM_Image>& images);

    // Switches the display interpretation and regenerates the texture.
    void SetViewMode(VRAMViewMode mode);
    VRAMViewMode GetViewMode() const { return view_mode; }

    // Real pixel size of the display texture in the current mode
    // (e.g. 4096x512 for Indexed4BPP, 1024x512 for Direct16BPP).
    int GetViewWidth() const;
    int GetViewHeight() const { return kHeight; }

    uint32_t GetVRAMTextureID() const { return vram_gl_texture; }

    // Decodes one full texpage tile (256/128/64 texels wide by 256 tall,
    // depending on tsb's color mode) directly from the raw VRAM buffer into
    // an RGBA8 buffer, using the CLUT at `cba` for indexed modes (ignored
    // for the 16bpp direct mode). Unlike the display texture above (which
    // is decoded in a single globally-selected view mode), this reads
    // whatever bpp `tsb` itself specifies - needed because a 3D scene can
    // reference several different bpp texpages/CLUTs at once. Used by the
    // 3D viewer to build one GL texture per distinct (tsb,cba) pair a
    // model's polygons actually reference.
    void DecodeTexPage(uint16_t tsb, uint16_t cba, std::vector<uint8_t>& out_rgba, int& out_width) const;

private:
    std::vector<uint16_t> vram_buffer;       // Actual VRAM content (BGR555, always 1024x512 words)
    std::vector<uint8_t> rgb_texture_buffer; // RGBA8 buffer, rebuilt on content/mode change
    uint32_t vram_gl_texture;
    VRAMViewMode view_mode = VRAMViewMode::Direct16BPP;

    void WriteClut(const TIM_Image& tim);
    void WriteImagePixels(const TIM_Image& tim);
    void UpdateGLTexture();
};
