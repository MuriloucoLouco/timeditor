#include "tim_texture_builder.h"
#include <GL/gl.h>

namespace gfx {

namespace {

inline void WriteBGR555AsRGBA(uint16_t color, std::vector<uint8_t>& rgba, int pixel_idx) {
    rgba[pixel_idx + 0] = (color & 0x1F) << 3;
    rgba[pixel_idx + 1] = ((color >> 5) & 0x1F) << 3;
    rgba[pixel_idx + 2] = ((color >> 10) & 0x1F) << 3;
    rgba[pixel_idx + 3] = (color == 0) ? 0 : 255; // Cor 0x0000 é transparente por convenção do PS1
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
    }
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
                break; // Formato desconhecido: aborta essa página para não corromper o buffer
            }
        }
    }

    return rgba;
}

} // namespace gfx
