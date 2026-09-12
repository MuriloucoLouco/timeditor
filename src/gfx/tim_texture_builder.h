#pragma once
#include "../core/tim_format.h"
#include <vector>
#include <cstdint>

namespace gfx {

// Decodes a TIM's raw pixel data (indexed or direct-color) into one OpenGL
// texture per CLUT, storing the resulting ids on TIM_Image::opengl_texture_ids.
class TIMTextureBuilder {
public:
    static void BuildTextures(TIM_Image& tim);

    // Releases the GL textures BuildTextures created for this TIM and clears
    // opengl_texture_ids. Must be called before a TIM_Image is dropped (e.g.
    // closing its file) - otherwise its GPU textures leak for the app's
    // lifetime, since TIM_Image itself has no GL-aware destructor.
    static void DeleteTextures(TIM_Image& tim);

    // Decodes one page (a single CLUT, or the whole image for direct-color
    // formats) into an RGBA8 buffer, real_width * height * 4 bytes, row-major.
    // Public so ImageExporter can reuse the exact same decoding for file export.
    static std::vector<uint8_t> DecodeToRGBA(const TIM_Image& tim, int clut_index);
};

} // namespace gfx
