#pragma once
#include "../core/vram_manager.h"
#include <cstdint>
#include <vector>

namespace gfx {

// Lazily decodes and caches one GL texture per distinct (tsb,cba) texpage a
// TMD model's polygons reference, straight from VRAMManager's raw buffer
// (see VRAMManager::DecodeTexPage) - a 3D scene can mix several different
// bpp texpages/CLUTs at once, which a single globally-decoded VRAM display
// texture can't represent. The cache is invalidated wholesale whenever
// VRAM's content actually changes (tracked via Document::GetVramVersion()),
// so edits made elsewhere in the app show up next time the 3D viewer draws.
class TmdTextureCache {
public:
    ~TmdTextureCache();

    // Returns the width (in texels) and GL texture id for the tile at
    // (tsb,cba), decoding it now if this is the first time it's been seen
    // since the last invalidation.
    struct Tile {
        uint32_t gl_tex = 0;
        int width = 0;
    };
    Tile GetTile(const VRAMManager& vram, uint16_t tsb, uint16_t cba);

    // Drops every cached texture. Call when `vram_version` changes.
    void InvalidateAll();

private:
    struct Entry {
        uint16_t tsb, cba;
        Tile tile;
    };
    std::vector<Entry> entries;
};

} // namespace gfx
