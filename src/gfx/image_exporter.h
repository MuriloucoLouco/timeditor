#pragma once
#include "../core/tim_format.h"
#include <string>

namespace gfx {

enum class ExportFormat { BMP, PNG, JPG };

// Writes a decoded TIM (using its currently selected CLUT, see
// TIM_Image::selected_clut) out as a standalone raster image file.
class ImageExporter {
public:
    // `jpg_quality` (1-100) is only used when format == JPG; JPEG has no
    // alpha channel, so the PS1's color-key transparency is flattened away.
    static bool Export(const TIM_Image& tim, const std::string& filepath, ExportFormat format, int jpg_quality);

    static const char* ExtensionFor(ExportFormat format);
};

} // namespace gfx
