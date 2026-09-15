#include "tim_texture_builder.h"
#include "../core/gl_compat.h"

namespace gfx {

namespace {

inline void WriteBGR555AsRGBA(uint16_t color, std::vector<uint8_t>& rgba, int pixel_idx) {
    rgba[pixel_idx + 0] = (color & 0x1F) << 3;
    rgba[pixel_idx + 1] = ((color >> 5) & 0x1F) << 3;
    rgba[pixel_idx + 2] = ((color >> 10) & 0x1F) << 3;
    rgba[pixel_idx + 3] = (color == 0) ? 0 : 255; // Color 0x0000 is transparent by PS1 convention
}

} // namespace

void TIMTextureBuilder::BuildTextures(TIM_Image& tim) {
    int num_textures = tim.has_clut ? tim.clut_header.num_cluts : 1;
    tim.opengl_texture_ids.resize(num_textures, 0);
    glGenTextures(num_textures, tim.opengl_texture_ids.data());

    for (int t = 0; t < num_textures; t++) {
        std::vector<uint8_t> rgba = DecodeToRGBA(tim, t);

        glBindTexture(GL_TEXTURE_2D, tim.opengl_texture_ids[t]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tim.real_width, tim.image_header.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        // CLAMP_TO_EDGE avoids edge bleeding when the zoom lands on a non-integer scale.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

void TIMTextureBuilder::DeleteTextures(TIM_Image& tim) {
    if (!tim.opengl_texture_ids.empty()) {
        glDeleteTextures(static_cast<GLsizei>(tim.opengl_texture_ids.size()), tim.opengl_texture_ids.data());
        tim.opengl_texture_ids.clear();
    }
}

void TIMTextureBuilder::RebuildTextures(TIM_Image& tim) {
    DeleteTextures(tim);
    BuildTextures(tim);
}

std::vector<uint8_t> TIMTextureBuilder::DecodeToRGBA(const TIM_Image& tim, int clut_index) {
    std::vector<uint8_t> rgba(static_cast<size_t>(tim.real_width) * tim.image_header.height * 4, 0);
    size_t data_idx = 0;

    for (int y = 0; y < tim.image_header.height; y++) {
        for (int x = 0; x < tim.real_width; ) {
            if (data_idx >= tim.image_data.size()) break;
            int pixel_idx = (y * tim.real_width + x) * 4;

            if (tim.bpp == 4) {
                uint8_t byte = tim.image_data[data_idx++];
                uint8_t p1 = byte & 0x0F;
                uint8_t p2 = (byte >> 4) & 0x0F;

                WriteBGR555AsRGBA(tim.clut_data[clut_index * 16 + p1], rgba, pixel_idx);
                x++;

                if (x < tim.real_width) {
                    WriteBGR555AsRGBA(tim.clut_data[clut_index * 16 + p2], rgba, pixel_idx + 4);
                    x++;
                }
            } else if (tim.bpp == 8) {
                uint8_t index = tim.image_data[data_idx++];
                WriteBGR555AsRGBA(tim.clut_data[clut_index * 256 + index], rgba, pixel_idx);
                x++;
            } else if (tim.bpp == 16) {
                if (data_idx + 1 >= tim.image_data.size()) break;
                uint16_t c = tim.image_data[data_idx] | (tim.image_data[data_idx + 1] << 8);
                data_idx += 2;
                WriteBGR555AsRGBA(c, rgba, pixel_idx);
                x++;
            } else if (tim.bpp == 24) {
                if (data_idx + 2 >= tim.image_data.size()) break;
                rgba[pixel_idx + 0] = tim.image_data[data_idx++];
                rgba[pixel_idx + 1] = tim.image_data[data_idx++];
                rgba[pixel_idx + 2] = tim.image_data[data_idx++];
                rgba[pixel_idx + 3] = 255;
                x++;
            } else {
                break; // Unknown format, stop to avoid corrupting the buffer
            }
        }
    }

    return rgba;
}

void TIMTextureBuilder::EnsureMasterImage(TIM_Image& tim) {
    if (!tim.master_rgba.empty()) return;

    int h = tim.image_header.height;
    tim.master_width = tim.real_width;
    tim.master_rgba = DecodeToRGBA(tim, tim.selected_clut);

    tim.master_index_map.clear();
    if (tim.bpp != 4 && tim.bpp != 8) return;

    tim.master_index_map.assign(static_cast<size_t>(tim.real_width) * h, 0);
    size_t data_idx = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < tim.real_width; ) {
            if (data_idx >= tim.image_data.size()) break;
            size_t p = static_cast<size_t>(y) * tim.real_width + x;

            if (tim.bpp == 4) {
                uint8_t byte = tim.image_data[data_idx++];
                tim.master_index_map[p] = byte & 0x0F;
                x++;
                if (x < tim.real_width) {
                    tim.master_index_map[p + 1] = (byte >> 4) & 0x0F;
                    x++;
                }
            } else {
                tim.master_index_map[p] = tim.image_data[data_idx++];
                x++;
            }
        }
    }
}

} // namespace gfx
