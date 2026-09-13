#pragma once
#include <cstdint>
#include <vector>

namespace gfx {

// Median-cut color quantization plus nearest-color index mapping and raw
// RGBA<->PS1 pixel packing. Shared by the Import dialog, the Image Editor's
// BPP switch-tabs, and per-stroke painting (which nearest-matches into an
// existing palette instead of regenerating it).
class ImageQuantizer {
public:
    struct Result {
        std::vector<uint16_t> palette; // BGR555, exactly color_count entries
        std::vector<uint8_t> indices;  // width*height, index into `palette`
    };

    // Reduces an RGBA8888 image to at most `color_count` colors via median
    // cut (population-weighted split on the widest channel of the bucket
    // with the largest spread, repeated until enough buckets exist).
    // Pixels with alpha < 128 are treated as transparent: they all map to
    // index 0, and index 0 is reserved as the 0x0000 color-key if any exist.
    static Result Quantize(const std::vector<uint8_t>& rgba, int width, int height, int color_count);

    // Nearest-match a single color into an existing palette by squared RGB
    // distance (alpha < 128 always maps to whichever index is the true
    // color-key 0x0000, if present, else nearest by color).
    static int NearestPaletteIndex(const std::vector<uint16_t>& palette, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    // BGR555 <-> RGBA8888, matching the color-key convention used
    // everywhere else in the app (word 0x0000 is fully transparent).
    static uint16_t RGBAToBGR555(uint8_t r, uint8_t g, uint8_t b);
    static void BGR555ToRGBA(uint16_t color, uint8_t out_rgba[4]);

    // `image_header.width` is in 16-bit words, not pixels; converts a pixel
    // width at a given bpp to that word count (rounding up).
    static int PixelWidthToWords(int pixel_width, int bpp);

    // Indexed/direct formats here are stored as one continuous nibble/byte
    // stream with no per-row padding (matching TIMTextureBuilder::DecodeToRGBA),
    // which only round-trips cleanly when each row starts on a word
    // boundary. Rounds `pixel_width` up to the nearest safe multiple for
    // `bpp` (4 for 4bpp, 2 for 8bpp, unchanged otherwise).
    static int RoundWidthForBpp(int pixel_width, int bpp);

    // Packs indices (0..color_count-1) into 4bpp/8bpp bytes, or packs RGBA
    // directly into 16bpp/24bpp bytes. Inverse of TIMTextureBuilder::DecodeToRGBA.
    static std::vector<uint8_t> PackIndexed(const std::vector<uint8_t>& indices, int width, int height, int bpp);
    static std::vector<uint8_t> PackDirect(const std::vector<uint8_t>& rgba, int width, int height, int bpp);
};

} // namespace gfx
