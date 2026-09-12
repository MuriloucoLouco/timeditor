#include "image_exporter.h"
#include "tim_texture_builder.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace gfx {

const char* ImageExporter::ExtensionFor(ExportFormat format) {
    switch (format) {
        case ExportFormat::BMP: return ".bmp";
        case ExportFormat::PNG: return ".png";
        case ExportFormat::JPG: return ".jpg";
    }
    return ".png";
}

bool ImageExporter::Export(const TIM_Image& tim, const std::string& filepath, ExportFormat format, int jpg_quality) {
    int w = tim.real_width;
    int h = tim.image_header.height;
    if (w <= 0 || h <= 0) return false;

    std::vector<uint8_t> rgba = TIMTextureBuilder::DecodeToRGBA(tim, tim.selected_clut);

    switch (format) {
        case ExportFormat::PNG:
            return stbi_write_png(filepath.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
        case ExportFormat::BMP:
            return stbi_write_bmp(filepath.c_str(), w, h, 4, rgba.data()) != 0;
        case ExportFormat::JPG: {
            // The stb JPEG writer only handles 1 or 3 components; drop alpha.
            std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
            size_t pixel_count = static_cast<size_t>(w) * h;
            for (size_t i = 0; i < pixel_count; i++) {
                rgb[i * 3 + 0] = rgba[i * 4 + 0];
                rgb[i * 3 + 1] = rgba[i * 4 + 1];
                rgb[i * 3 + 2] = rgba[i * 4 + 2];
            }
            return stbi_write_jpg(filepath.c_str(), w, h, 3, rgb.data(), jpg_quality) != 0;
        }
    }
    return false;
}

} // namespace gfx
