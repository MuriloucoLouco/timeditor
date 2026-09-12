#include "vram_manager.h"
#include <GL/gl.h>

VRAMManager::VRAMManager() {
    vram_buffer.resize(kWidth * kHeight, 0);
    vram_gl_texture = 0;
}

VRAMManager::~VRAMManager() {}

void VRAMManager::InitializeGL() {
    glGenTextures(1, &vram_gl_texture);
    glBindTexture(GL_TEXTURE_2D, vram_gl_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    UpdateGLTexture(); // Garante que a textura já exista (VRAM zerada) desde o início
}

void VRAMManager::WriteTIMToVRAM(const TIM_Image& tim) {
    WriteClut(tim);
    WriteImagePixels(tim);
    UpdateGLTexture();
}

void VRAMManager::SetViewMode(VRAMViewMode mode) {
    if (mode == view_mode) return;
    view_mode = mode;
    UpdateGLTexture();
}

int VRAMManager::GetViewWidth() const {
    switch (view_mode) {
        case VRAMViewMode::Indexed4BPP: return kWidth * 4;
        case VRAMViewMode::Indexed8BPP: return kWidth * 2;
        case VRAMViewMode::Direct16BPP:
        default: return kWidth;
    }
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
    const int view_width = GetViewWidth();
    rgb_texture_buffer.assign(static_cast<size_t>(view_width) * kHeight * 4, 0);

    // A VRAM é sempre armazenada como 1024x512 words de 16 bits — o que muda
    // por modo é só quantos pixels cada word representa na exibição:
    // 1 pixel direto (16 BPP), 2 índices de 8 bits, ou 4 índices de 4 bits.
    for (int y = 0; y < kHeight; y++) {
        for (int word_x = 0; word_x < kWidth; word_x++) {
            uint16_t word = vram_buffer[y * kWidth + word_x];

            switch (view_mode) {
                case VRAMViewMode::Direct16BPP: {
                    uint8_t r = (word & 0x001F) << 3;
                    uint8_t g = ((word & 0x03E0) >> 5) << 3;
                    uint8_t b = ((word & 0x7C00) >> 10) << 3;
                    uint8_t a = (word == 0) ? 0 : 255; // Transparência simplificada (bit STP ignorado)
                    int idx = (y * view_width + word_x) * 4;
                    rgb_texture_buffer[idx + 0] = r;
                    rgb_texture_buffer[idx + 1] = g;
                    rgb_texture_buffer[idx + 2] = b;
                    rgb_texture_buffer[idx + 3] = a;
                    break;
                }
                case VRAMViewMode::Indexed8BPP: {
                    // Cada word guarda 2 índices de 8 bits (byte baixo primeiro).
                    // Sem um CLUT específico escolhido, mostramos o índice cru
                    // como escala de cinza (já cobre a faixa 0-255 inteira).
                    uint8_t indices[2] = {
                        static_cast<uint8_t>(word & 0xFF),
                        static_cast<uint8_t>((word >> 8) & 0xFF)
                    };
                    for (int k = 0; k < 2; k++) {
                        int px = word_x * 2 + k;
                        uint8_t gray = indices[k];
                        int idx = (y * view_width + px) * 4;
                        rgb_texture_buffer[idx + 0] = gray;
                        rgb_texture_buffer[idx + 1] = gray;
                        rgb_texture_buffer[idx + 2] = gray;
                        rgb_texture_buffer[idx + 3] = 255;
                    }
                    break;
                }
                case VRAMViewMode::Indexed4BPP: {
                    // Cada word guarda 4 índices de 4 bits (nibble menos
                    // significativo primeiro). Escala de cinza 0-15 -> 0-255.
                    for (int k = 0; k < 4; k++) {
                        uint8_t nibble = (word >> (4 * k)) & 0x0F;
                        uint8_t gray = nibble * 17;
                        int px = word_x * 4 + k;
                        int idx = (y * view_width + px) * 4;
                        rgb_texture_buffer[idx + 0] = gray;
                        rgb_texture_buffer[idx + 1] = gray;
                        rgb_texture_buffer[idx + 2] = gray;
                        rgb_texture_buffer[idx + 3] = 255;
                    }
                    break;
                }
            }
        }
    }

    glBindTexture(GL_TEXTURE_2D, vram_gl_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, view_width, kHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgb_texture_buffer.data());
    // Reforça o filtro nearest a cada regeneração — como a textura pode ser
    // recriada com uma largura diferente por modo, é bom garantir que o
    // estado do sampler nunca dependa de ter sido setado só uma vez lá atrás.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}
