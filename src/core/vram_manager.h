#pragma once
#include "tim_format.h"
#include <vector>
#include <cstdint>

// Modo usado só para *interpretar a exibição* do conteúdo da VRAM — o
// conteúdo real em vram_buffer nunca muda, apenas a forma como cada word de
// 16 bits é decodificada para a textura de visualização.
enum class VRAMViewMode : uint8_t {
    Indexed4BPP, // Cada word vira 4 pixels (nibbles), mostrados em escala de cinza
    Indexed8BPP, // Cada word vira 2 pixels (bytes), mostrados em escala de cinza
    Direct16BPP, // Cada word é 1 pixel BGR555 (modo de cor direta nativo do PS1)
};

// Emula os 1024x512 pixels de 16 bits da VRAM do PS1 e mantém uma textura
// OpenGL espelhando seu conteúdo para visualização.
class VRAMManager {
public:
    static constexpr int kWidth = 1024; // Largura da VRAM em "words" de 16 bits
    static constexpr int kHeight = 512;

    VRAMManager();
    ~VRAMManager();

    void InitializeGL();

    // Escreve a CLUT (se houver) e os pixels de uma TIM na VRAM, respeitando
    // as coordenadas Image Org / Palette Org do próprio arquivo, e atualiza
    // a textura de visualização.
    void WriteTIMToVRAM(const TIM_Image& tim);

    // Troca o modo de interpretação da textura de visualização e a
    // regenera imediatamente (não mexe no conteúdo real da VRAM).
    void SetViewMode(VRAMViewMode mode);
    VRAMViewMode GetViewMode() const { return view_mode; }

    // Dimensões reais, em pixels, da textura de visualização no modo atual
    // (ex.: 4096x512 em Indexed4BPP, 2048x512 em Indexed8BPP, 1024x512 em Direct16BPP).
    int GetViewWidth() const;
    int GetViewHeight() const { return kHeight; }

    uint32_t GetVRAMTextureID() const { return vram_gl_texture; }

private:
    std::vector<uint16_t> vram_buffer;       // Conteúdo real da VRAM (BGR555, sempre 1024x512 words)
    std::vector<uint8_t> rgb_texture_buffer; // Buffer RGBA8 recalculado a cada troca de modo/conteúdo
    uint32_t vram_gl_texture;
    VRAMViewMode view_mode = VRAMViewMode::Direct16BPP;

    void WriteClut(const TIM_Image& tim);
    void WriteImagePixels(const TIM_Image& tim);
    void UpdateGLTexture();
};
