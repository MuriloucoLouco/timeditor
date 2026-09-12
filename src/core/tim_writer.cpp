#include "tim_writer.h"
#include <cstdio>

namespace tim {

namespace {

void AppendBytes(std::vector<uint8_t>& out, const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

} // namespace

void Writer::SerializeImage(const TIM_Image& img, std::vector<uint8_t>& out) {
    AppendBytes(out, &img.header, sizeof(TIM_Header));

    if (img.has_clut) {
        AppendBytes(out, &img.clut_header, sizeof(TIM_CLUT_Header));
        AppendBytes(out, img.clut_data.data(), img.clut_data.size() * sizeof(uint16_t));
    }

    AppendBytes(out, &img.image_header, sizeof(TIM_Image_Header));
    AppendBytes(out, img.image_data.data(), img.image_data.size());
}

bool Writer::WriteToFile(const std::string& filepath, const std::vector<const TIM_Image*>& images) {
    if (images.empty()) return false;

    std::vector<uint8_t> buffer;
    for (const TIM_Image* img : images) {
        SerializeImage(*img, buffer);
    }

    FILE* file = fopen(filepath.c_str(), "wb");
    if (!file) return false;

    bool ok = fwrite(buffer.data(), 1, buffer.size(), file) == buffer.size();
    fclose(file);
    return ok;
}

} // namespace tim
