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

// Emulates the PS1's 1024x512, 16-bit VRAM and keeps an OpenGL texture
// mirroring its content for display.
class VRAMManager {
public:
    static constexpr int kWidth = 1024; // VRAM width in 16-bit words
    static constexpr int kHeight = 512;

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

private:
    std::vector<uint16_t> vram_buffer;       // Actual VRAM content (BGR555, always 1024x512 words)
    std::vector<uint8_t> rgb_texture_buffer; // RGBA8 buffer, rebuilt on content/mode change
    uint32_t vram_gl_texture;
    VRAMViewMode view_mode = VRAMViewMode::Direct16BPP;

    void WriteClut(const TIM_Image& tim);
    void WriteImagePixels(const TIM_Image& tim);
    void UpdateGLTexture();
};
