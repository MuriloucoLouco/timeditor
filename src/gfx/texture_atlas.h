#pragma once
#include "../core/vram_manager.h"
#include <cstdint>
#include <utility>
#include <vector>

namespace gfx {

// Packs every distinct (tsb, cba) texture tile a model export references
// into one combined RGBA image, so the exported bundle references a single
// texture file instead of one PNG per material - see
// TmdObjExport::Options::pack_atlas / TmdGltfExport::Options::pack_atlas.
//
// Every PS1 texpage tile is VRAMManager::kTPageHeight (256) pixels tall and
// TileWidthForTsb(tsb)-wide (256/128/64, by color mode) - packing exploits
// that fixed height with simple shelf packing: tiles are laid out left to
// right, wrapping to a new row once a row would exceed kMaxRowWidth, so
// there's no need for a general bin-packer here.
struct AtlasTile {
    uint16_t tsb = 0;
    uint16_t cba = 0;
    int width = 256;
    int x = 0, y = 0; // top-left placement in the packed atlas, pixels
};

struct AtlasResult {
    std::vector<uint8_t> rgba; // width*height*4, RGBA8
    int width = 0;
    int height = 0;
    std::vector<AtlasTile> tiles; // same order as `keys`, one per entry
};

// `keys`: distinct (tsb, cba) pairs to pack, each decoded via
// VRAMManager::DecodeTexPage. Passing the same pair twice wastes atlas
// space but isn't otherwise wrong - callers should dedupe first.
AtlasResult PackTextureAtlas(const VRAMManager& vram_manager, const std::vector<std::pair<uint16_t, uint16_t>>& keys);

} // namespace gfx
