#include "vram_manager.h"
#include <GL/gl.h>
#include <algorithm>

VRAMManager::VRAMManager() {
    vram_buffer.resize(kWidth * kHeight, 0);
    vram_gl_texture = 0;
}

TPageLocation VRAMManager::ComputeTPageLocation(int x_words, int y) {
    TPageLocation loc;
    loc.tpage_id = (y / kTPageHeight) * kTPagesPerRow + (x_words / kTPageWidthWords);
    loc.local_x_words = x_words % kTPageWidthWords;
    loc.local_y = y % kTPageHeight;
    return loc;
}

VRAMManager::~VRAMManager() {}

void VRAMManager::InitializeGL() {
    glGenTextures(1, &vram_gl_texture);
    glBindTexture(GL_TEXTURE_2D, vram_gl_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    UpdateGLTexture(); // Texture exists (blank VRAM) from the start
}

void VRAMManager::RebuildFromImages(const std::vector<TIM_Image>& images) {
    std::fill(vram_buffer.begin(), vram_buffer.end(), 0);
    for (const auto& tim : images) {
        WriteClut(tim);
        WriteImagePixels(tim);
    }
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

    // width is in native 16-bit words, so every pixel here consumes exactly
    // 2 bytes regardless of the TIM's logical bpp - VRAM only ever sees words.
    for (int y = 0; y < tim.image_header.height; y++) {
        for (int x = 0; x < tim.image_header.width; x++) {
            int px = ix + x;
            int py = iy + y;
            if (px < kWidth && py < kHeight && data_idx + 1 < tim.image_data.size()) {
                uint16_t word = tim.image_data[data_idx] | (tim.image_data[data_idx + 1] << 8);
                vram_buffer[py * kWidth + px] = word;
                data_idx += 2;
            }
        }
    }
}

void VRAMManager::DecodeTexPage(uint16_t tsb, uint16_t cba, std::vector<uint8_t>& out_rgba, int& out_width) const {
    int tpage_x_words = (tsb & 0xF) * kTPageWidthWords;
    int tpage_y = ((tsb >> 4) & 0x1) * kTPageHeight;
    int color_mode = (tsb >> 7) & 0x3;
    int clut_x = (cba % 64) * 16;
    int clut_y = cba / 64;

    int texels_per_word = (color_mode == 0) ? 4 : (color_mode == 1) ? 2 : 1;
    out_width = kTPageWidthWords * texels_per_word;
    out_rgba.assign(static_cast<size_t>(out_width) * kTPageHeight * 4, 0);

    for (int y = 0; y < kTPageHeight; y++) {
        int vy = tpage_y + y;
        if (vy >= kHeight) continue;
        for (int x = 0; x < out_width; x++) {
            uint16_t color16 = 0;
            if (color_mode == 2) {
                int vx = tpage_x_words + x;
                if (vx < kWidth) color16 = vram_buffer[vy * kWidth + vx];
            } else {
                int word_offset = x / texels_per_word;
                int vx = tpage_x_words + word_offset;
                uint16_t word = (vx < kWidth) ? vram_buffer[vy * kWidth + vx] : 0;
                int index;
                if (color_mode == 0) {
                    int nibble = x % 4;
                    index = (word >> (nibble * 4)) & 0xF;
                } else {
                    int byte_sel = x % 2;
                    index = byte_sel ? ((word >> 8) & 0xFF) : (word & 0xFF);
                }
                int cx = clut_x + index;
                if (cx < kWidth && clut_y < kHeight) color16 = vram_buffer[clut_y * kWidth + cx];
            }

            uint8_t r = (color16 & 0x001F) << 3;
            uint8_t g = ((color16 & 0x03E0) >> 5) << 3;
            uint8_t b = ((color16 & 0x7C00) >> 10) << 3;
            uint8_t a = (color16 == 0) ? 0 : 255;
            int idx = (y * out_width + x) * 4;
            out_rgba[idx + 0] = r;
            out_rgba[idx + 1] = g;
            out_rgba[idx + 2] = b;
            out_rgba[idx + 3] = a;
        }
    }
}

void VRAMManager::UpdateGLTexture() {
    const int view_width = GetViewWidth();
    rgb_texture_buffer.assign(static_cast<size_t>(view_width) * kHeight * 4, 0);

    for (int y = 0; y < kHeight; y++) {
        for (int word_x = 0; word_x < kWidth; word_x++) {
            uint16_t word = vram_buffer[y * kWidth + word_x];

            switch (view_mode) {
                case VRAMViewMode::Direct16BPP: {
                    uint8_t r = (word & 0x001F) << 3;
                    uint8_t g = ((word & 0x03E0) >> 5) << 3;
                    uint8_t b = ((word & 0x7C00) >> 10) << 3;
                    uint8_t a = (word == 0) ? 0 : 255;
                    int idx = (y * view_width + word_x) * 4;
                    rgb_texture_buffer[idx + 0] = r;
                    rgb_texture_buffer[idx + 1] = g;
                    rgb_texture_buffer[idx + 2] = b;
                    rgb_texture_buffer[idx + 3] = a;
                    break;
                }
                case VRAMViewMode::Indexed8BPP: {
                    // Two 8-bit indices per word (low byte first). With no
                    // CLUT selected, shown as grayscale.
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
                    // Four 4-bit indices per word, scaled 0-15 -> 0-255.
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}
