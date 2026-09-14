#include "texture_atlas.h"
#include <algorithm>

namespace gfx {

namespace {
// Wide enough that a typical model's handful of tiles fits in 1-3 rows
// without the atlas turning needlessly tall; not a hard constraint on
// anything downstream (UVs are computed from the final atlas size either way).
constexpr int kMaxRowWidth = 1024;
} // namespace

AtlasResult PackTextureAtlas(const VRAMManager& vram_manager, const std::vector<std::pair<uint16_t, uint16_t>>& keys) {
    AtlasResult result;
    if (keys.empty()) return result;

    struct Decoded {
        uint16_t tsb = 0, cba = 0;
        int width = 0;
        std::vector<uint8_t> rgba;
    };
    std::vector<Decoded> decoded;
    decoded.reserve(keys.size());
    for (const auto& key : keys) {
        Decoded d;
        d.tsb = key.first;
        d.cba = key.second;
        vram_manager.DecodeTexPage(key.first, key.second, d.rgba, d.width);
        decoded.push_back(std::move(d));
    }

    // Shelf-pack left to right, wrapping rows at kMaxRowWidth - every tile
    // is exactly kTPageHeight tall, so there's no need to sort by height or
    // track a skyline; the next row is always exactly kTPageHeight further down.
    int cursor_x = 0, cursor_y = 0;
    int atlas_width = 0;
    result.tiles.reserve(decoded.size());
    for (const auto& d : decoded) {
        if (cursor_x > 0 && cursor_x + d.width > kMaxRowWidth) {
            cursor_x = 0;
            cursor_y += VRAMManager::kTPageHeight;
        }
        AtlasTile tile;
        tile.tsb = d.tsb;
        tile.cba = d.cba;
        tile.width = d.width;
        tile.x = cursor_x;
        tile.y = cursor_y;
        result.tiles.push_back(tile);

        cursor_x += d.width;
        atlas_width = std::max(atlas_width, cursor_x);
    }
    int atlas_height = cursor_y + VRAMManager::kTPageHeight;

    result.width = atlas_width;
    result.height = atlas_height;
    result.rgba.assign(static_cast<size_t>(atlas_width) * atlas_height * 4, 0);

    for (size_t i = 0; i < decoded.size(); i++) {
        const auto& d = decoded[i];
        const auto& tile = result.tiles[i];
        for (int y = 0; y < VRAMManager::kTPageHeight; y++) {
            const uint8_t* src_row = &d.rgba[static_cast<size_t>(y) * d.width * 4];
            uint8_t* dst_row = &result.rgba[(static_cast<size_t>(tile.y + y) * atlas_width + tile.x) * 4];
            std::copy(src_row, src_row + static_cast<size_t>(d.width) * 4, dst_row);
        }
    }

    return result;
}

} // namespace gfx
