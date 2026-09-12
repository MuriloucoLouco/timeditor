#pragma once
#include "../core/tim_format.h"
#include <vector>
#include <cstdint>

namespace gfx {

// Decodifica os dados de pixel crus de uma TIM_Image (indexados por CLUT ou
// de cor direta) e gera uma textura OpenGL por CLUT, gravando os IDs de
// volta em TIM_Image::opengl_texture_ids.
class TIMTextureBuilder {
public:
    static void BuildTextures(TIM_Image& tim);

private:
    // Decodifica uma única "página" (um CLUT específico, ou a imagem inteira
    // para formatos sem paleta) para um buffer RGBA8 pronto para glTexImage2D.
    static std::vector<uint8_t> DecodeToRGBA(const TIM_Image& tim, int clut_index);
};

} // namespace gfx
