#include "tmd_export_material.h"

namespace gfx {

int TileWidthForTsb(uint16_t tsb) {
    int color_mode = (tsb >> 7) & 0x3;
    return color_mode == 0 ? 256 : color_mode == 1 ? 128 : 64;
}

ExportMaterial MakeMaterialKey(const tmd::TMD_Polygon& p) {
    ExportMaterial m;
    m.tsb = p.tsb;
    m.cba = p.cba;
    m.tile_width = p.textured ? TileWidthForTsb(p.tsb) : 256;
    m.textured = p.textured;
    m.semi_transparent = p.semi_transparent;
    m.color_per_vertex = p.color_per_vertex;
    for (int i = 0; i < 4; i++) {
        m.color[i][0] = p.color[i][0];
        m.color[i][1] = p.color[i][1];
        m.color[i][2] = p.color[i][2];
    }
    return m;
}

bool SameMaterial(const ExportMaterial& a, const ExportMaterial& b) {
    if (a.tsb != b.tsb || a.cba != b.cba || a.textured != b.textured || a.semi_transparent != b.semi_transparent ||
        a.color_per_vertex != b.color_per_vertex) {
        return false;
    }
    int n = a.color_per_vertex ? 4 : 1;
    for (int i = 0; i < n; i++) {
        if (a.color[i][0] != b.color[i][0] || a.color[i][1] != b.color[i][1] || a.color[i][2] != b.color[i][2]) {
            return false;
        }
    }
    return true;
}

} // namespace gfx
