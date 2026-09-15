#pragma once
#include "../core/tmd_format.h"
#include <cstdint>
#include <string>

namespace gfx {

// One distinct combination of PS1-specific polygon attributes an exported
// material stands in for - purely to group polygons for the .mtl/glTF-
// material and baked-texture output. Export-only: nothing reads this back
// on import (see tmd_mesh_import.h - reimporting always rebuilds fresh
// instead of trying to recover the original attributes). Shared by
// TmdObjExport and TmdGltfExport, which otherwise have nothing in common
// (OBJ writes text, glTF writes indexed binary buffers) - this material-
// grouping logic was previously an identical copy in each.
struct ExportMaterial {
    std::string name;
    uint16_t tsb = 0;
    uint16_t cba = 0;
    int tile_width = 256;
    bool textured = false;
    bool semi_transparent = false;
    bool color_per_vertex = false;
    uint8_t color[4][3] = { { 128, 128, 128 }, { 128, 128, 128 }, { 128, 128, 128 }, { 128, 128, 128 } };
};

int TileWidthForTsb(uint16_t tsb);
ExportMaterial MakeMaterialKey(const tmd::TMD_Polygon& p);
bool SameMaterial(const ExportMaterial& a, const ExportMaterial& b);

} // namespace gfx
