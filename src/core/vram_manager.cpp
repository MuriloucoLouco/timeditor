#include "vram_manager.h"
#include <GL/gl.h>

VRAMManager::VRAMManager() {
    vram_buffer.resize(kWidth * kHeight, 0);
    rgb_texture_buffer.resize(kWidth * kHeight * 4, 0);
    vram_gl_texture = 0;
}

VRAMManager::~VRAMManager() {}

void VRAMManager::InitializeGL() {
    glGenTextures(1, &vram_gl_texture);
    glBindTexture(GL_TEXTURE_2D, vram_gl_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void VRAMManager::WriteTIMToVRAM(const TIM_Image& tim) {
    WriteClut(tim);
    WriteImagePixels(tim);
    UpdateGLTexture();
}

void VRAMManager::WriteClut(const TIM_Image& tim) {
    if (!tim.has_clut) return;

    int cx = tim.clut_header.origin_x;
    int cy = tim.clut_header.origin_y;
    int colors = tim.clut_header.colors_per_clut;

    for (int c = 0; c < tim.clut_header.num_cluts; c++) {
        for (int i = 0; i < colors; i++) {
            int px = cx + i;
            int py = cy + c;
            if (px < kWidth && py < kHeight) {
                vram_buffer[py * kWidth + px] = tim.clut_data[c * colors + i];
            }
        }
    }
}

void VRAMManager::WriteImagePixels(const TIM_Image& tim) {
    int ix = tim.image_header.origin_x;
    int iy = tim.image_header.origin_y;
    size_t data_idx = 0;

    // width aqui está em "words" de 16 bits nativos do PS1, então cada pixel
    // consome exatamente 2 bytes de image_data independente do bpp lógico —
    // é assim que a VRAM enxerga qualquer TIM, decodificado ou não.
    for (int y = 0; y < tim.image_header.height; y++) {
        for (int x = 0; x < tim.image_header.width; x++) {
            int px = ix + x;
            int py = iy + y;
            if (px < kWidth && py < kHeight && data_idx + 1 < tim.image_data.size()) {
                uint16_t vram_word = tim.image_data[data_idx] | (tim.image_data[data_idx + 1] << 8);
                vram_buffer[py * kWidth + px] = vram_word;
                data_idx += 2;
            }
        }
    }
}

void VRAMManager::UpdateGLTexture() {
    for (int i = 0; i < kWidth * kHeight; i++) {
        uint16_t bgr15 = vram_buffer[i];

        // Conversão de BGR555 (formato nativo do PS1) para RGBA8 (para exibição em OpenGL).
        uint8_t r = (bgr15 & 0x001F) << 3;
        uint8_t g = ((bgr15 & 0x03E0) >> 5) << 3;
        uint8_t b = ((bgr15 & 0x7C00) >> 10) << 3;
        uint8_t a = (bgr15 == 0) ? 0 : 255; // Transparência simplificada (bit STP ignorado)

        rgb_texture_buffer[i * 4 + 0] = r;
        rgb_texture_buffer[i * 4 + 1] = g;
        rgb_texture_buffer[i * 4 + 2] = b;
        rgb_texture_buffer[i * 4 + 3] = a;
    }

    glBindTexture(GL_TEXTURE_2D, vram_gl_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kWidth, kHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgb_texture_buffer.data());
}
