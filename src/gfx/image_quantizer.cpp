#include "image_quantizer.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace gfx {

namespace {

struct ColorCount {
    uint8_t r, g, b;
    int count;
};

struct Bucket {
    std::vector<int> members; // indices into the ColorCount list
};

void BucketChannelRange(const std::vector<ColorCount>& colors, const Bucket& bucket, int channel,
                         uint8_t& out_min, uint8_t& out_max) {
    out_min = 255;
    out_max = 0;
    for (int idx : bucket.members) {
        uint8_t v = channel == 0 ? colors[idx].r : channel == 1 ? colors[idx].g : colors[idx].b;
        out_min = std::min(out_min, v);
        out_max = std::max(out_max, v);
    }
}

} // namespace

uint16_t ImageQuantizer::RGBAToBGR555(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t r5 = r >> 3, g5 = g >> 3, b5 = b >> 3;
    return static_cast<uint16_t>(r5 | (g5 << 5) | (b5 << 10));
}

void ImageQuantizer::BGR555ToRGBA(uint16_t color, uint8_t out_rgba[4]) {
    out_rgba[0] = (color & 0x1F) << 3;
    out_rgba[1] = ((color >> 5) & 0x1F) << 3;
    out_rgba[2] = ((color >> 10) & 0x1F) << 3;
    out_rgba[3] = (color == 0) ? 0 : 255;
}

int ImageQuantizer::NearestPaletteIndex(const std::vector<uint16_t>& palette, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (palette.empty()) return 0;
    if (a < 128) {
        for (int i = 0; i < static_cast<int>(palette.size()); i++) {
            if (palette[i] == 0x0000) return i;
        }
    }
    int best = 0;
    long best_dist = -1;
    for (int i = 0; i < static_cast<int>(palette.size()); i++) {
        uint8_t rgba[4];
        BGR555ToRGBA(palette[i], rgba);
        long dr = static_cast<long>(rgba[0]) - r;
        long dg = static_cast<long>(rgba[1]) - g;
        long db = static_cast<long>(rgba[2]) - b;
        long dist = dr * dr + dg * dg + db * db;
        if (best_dist < 0 || dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

ImageQuantizer::Result ImageQuantizer::Quantize(const std::vector<uint8_t>& rgba, int width, int height, int color_count) {
    Result result;
    size_t pixel_count = static_cast<size_t>(width) * height;
    result.indices.assign(pixel_count, 0);
    if (pixel_count == 0 || color_count <= 0) return result;

    // Gather unique opaque colors with population counts.
    std::unordered_map<uint32_t, int> color_to_index;
    std::vector<ColorCount> colors;
    bool has_transparent = false;

    for (size_t p = 0; p < pixel_count; p++) {
        uint8_t r = rgba[p * 4 + 0], g = rgba[p * 4 + 1], b = rgba[p * 4 + 2], a = rgba[p * 4 + 3];
        if (a < 128) {
            has_transparent = true;
            continue;
        }
        uint32_t key = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
        auto it = color_to_index.find(key);
        if (it == color_to_index.end()) {
            color_to_index[key] = static_cast<int>(colors.size());
            colors.push_back({ r, g, b, 1 });
        } else {
            colors[it->second].count++;
        }
    }

    int reserved = has_transparent ? 1 : 0;
    int target_buckets = std::max(1, color_count - reserved);

    std::vector<uint16_t> palette;
    if (has_transparent) palette.push_back(0x0000);

    if (colors.empty()) {
        while (static_cast<int>(palette.size()) < color_count) palette.push_back(0x0000);
        result.palette = std::move(palette);
        return result; // indices already all zero
    }

    std::vector<Bucket> buckets(1);
    buckets[0].members.reserve(colors.size());
    for (int i = 0; i < static_cast<int>(colors.size()); i++) buckets[0].members.push_back(i);

    while (static_cast<int>(buckets.size()) < target_buckets) {
        int best_bucket = -1, best_channel = 0, best_range = -1;
        for (int bi = 0; bi < static_cast<int>(buckets.size()); bi++) {
            if (buckets[bi].members.size() <= 1) continue;
            for (int ch = 0; ch < 3; ch++) {
                uint8_t mn, mx;
                BucketChannelRange(colors, buckets[bi], ch, mn, mx);
                int range = mx - mn;
                if (range > best_range) {
                    best_range = range;
                    best_bucket = bi;
                    best_channel = ch;
                }
            }
        }
        if (best_bucket < 0 || best_range <= 0) break; // nothing left worth splitting

        Bucket& bucket = buckets[best_bucket];
        std::sort(bucket.members.begin(), bucket.members.end(), [&](int a, int b) {
            uint8_t va = best_channel == 0 ? colors[a].r : best_channel == 1 ? colors[a].g : colors[a].b;
            uint8_t vb = best_channel == 0 ? colors[b].r : best_channel == 1 ? colors[b].g : colors[b].b;
            return va < vb;
        });

        // Split at the population-weighted median so both halves end up
        // with roughly equal total pixel weight, not just equal color count.
        long total_weight = 0;
        for (int idx : bucket.members) total_weight += colors[idx].count;
        long half = total_weight / 2;
        long running = 0;
        size_t split_at = bucket.members.size() / 2;
        for (size_t i = 0; i < bucket.members.size(); i++) {
            running += colors[bucket.members[i]].count;
            if (running >= half) {
                split_at = i + 1;
                break;
            }
        }
        split_at = std::clamp(split_at, size_t(1), bucket.members.size() - 1);

        Bucket new_bucket;
        new_bucket.members.assign(bucket.members.begin() + static_cast<long>(split_at), bucket.members.end());
        bucket.members.erase(bucket.members.begin() + static_cast<long>(split_at), bucket.members.end());
        buckets.push_back(std::move(new_bucket));
    }

    for (auto& bucket : buckets) {
        long sum_r = 0, sum_g = 0, sum_b = 0, weight = 0;
        for (int idx : bucket.members) {
            sum_r += static_cast<long>(colors[idx].r) * colors[idx].count;
            sum_g += static_cast<long>(colors[idx].g) * colors[idx].count;
            sum_b += static_cast<long>(colors[idx].b) * colors[idx].count;
            weight += colors[idx].count;
        }
        uint8_t r = weight ? static_cast<uint8_t>(sum_r / weight) : 0;
        uint8_t g = weight ? static_cast<uint8_t>(sum_g / weight) : 0;
        uint8_t b = weight ? static_cast<uint8_t>(sum_b / weight) : 0;
        palette.push_back(RGBAToBGR555(r, g, b));
    }
    while (static_cast<int>(palette.size()) < color_count) palette.push_back(palette.back());

    result.palette = palette;
    for (size_t p = 0; p < pixel_count; p++) {
        uint8_t r = rgba[p * 4 + 0], g = rgba[p * 4 + 1], b = rgba[p * 4 + 2], a = rgba[p * 4 + 3];
        result.indices[p] = static_cast<uint8_t>(NearestPaletteIndex(palette, r, g, b, a));
    }
    return result;
}

int ImageQuantizer::PixelWidthToWords(int pixel_width, int bpp) {
    switch (bpp) {
        case 4: return (pixel_width + 3) / 4;
        case 8: return (pixel_width + 1) / 2;
        case 24: return static_cast<int>(std::ceil(pixel_width * 1.5));
        default: return pixel_width; // 16bpp: 1 word per pixel
    }
}

int ImageQuantizer::RoundWidthForBpp(int pixel_width, int bpp) {
    if (bpp == 4) return ((pixel_width + 3) / 4) * 4;
    if (bpp == 8) return ((pixel_width + 1) / 2) * 2;
    return pixel_width;
}

std::vector<uint8_t> ImageQuantizer::PackIndexed(const std::vector<uint8_t>& indices, int width, int height, int bpp) {
    size_t pixel_count = static_cast<size_t>(width) * height;
    std::vector<uint8_t> out;
    if (bpp == 4) {
        out.assign((pixel_count + 1) / 2, 0);
        for (size_t p = 0; p < pixel_count; p++) {
            uint8_t idx = indices[p] & 0x0F;
            if (p % 2 == 0) out[p / 2] |= idx;
            else out[p / 2] |= static_cast<uint8_t>(idx << 4);
        }
    } else { // 8bpp
        out.assign(pixel_count, 0);
        for (size_t p = 0; p < pixel_count; p++) out[p] = indices[p];
    }
    return out;
}

std::vector<uint8_t> ImageQuantizer::PackDirect(const std::vector<uint8_t>& rgba, int width, int height, int bpp) {
    size_t pixel_count = static_cast<size_t>(width) * height;
    std::vector<uint8_t> out;
    if (bpp == 16) {
        out.assign(pixel_count * 2, 0);
        for (size_t p = 0; p < pixel_count; p++) {
            uint16_t word = RGBAToBGR555(rgba[p * 4 + 0], rgba[p * 4 + 1], rgba[p * 4 + 2]);
            out[p * 2 + 0] = word & 0xFF;
            out[p * 2 + 1] = (word >> 8) & 0xFF;
        }
    } else { // 24bpp
        out.assign(pixel_count * 3, 0);
        for (size_t p = 0; p < pixel_count; p++) {
            out[p * 3 + 0] = rgba[p * 4 + 0];
            out[p * 3 + 1] = rgba[p * 4 + 1];
            out[p * 3 + 2] = rgba[p * 4 + 2];
        }
    }
    return out;
}

} // namespace gfx
