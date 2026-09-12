#pragma once
#include "tim_format.h"
#include <vector>
#include <cstdint>

// Emula os 1024x512 pixels de 16 bits da VRAM do PS1 e mantém uma textura
// OpenGL espelhando seu conteúdo para visualização.
class VRAMManager {
public:
    static constexpr int kWidth = 1024;
    static constexpr int kHeight = 512;

    VRAMManager();
    ~VRAMManager();

    void InitializeGL();

    // Escreve a CLUT (se houver) e os pixels de uma TIM na VRAM, respeitando
    // as coordenadas Image Org / Palette Org do próprio arquivo, e atualiza
    // a textura de visualização.
    void WriteTIMToVRAM(const TIM_Image& tim);

    uint32_t GetVRAMTextureID() const { return vram_gl_texture; }

private:
    std::vector<uint16_t> vram_buffer;       // Conteúdo real da VRAM (BGR555)
    std::vector<uint8_t> rgb_texture_buffer; // Cópia convertida para RGBA8, usada só para exibição
    uint32_t vram_gl_texture;

    void WriteClut(const TIM_Image& tim);
    void WriteImagePixels(const TIM_Image& tim);
    void UpdateGLTexture();
};
