#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace tmd {

struct TMD_Vertex {
    int16_t x = 0, y = 0, z = 0;
};

struct TMD_Normal {
    int16_t x = 0, y = 0, z = 0;
};

// One decoded triangle or quad, unified across every PS1 GTE primitive
// variant the format defines (flat/gouraud shaded, textured/untextured,
// lit/unlit, 3 or 4 sided) - see tmd_parser.cpp for how the raw mode/flag
// bitfields select which of those a given primitive actually is. Fields
// that don't apply to a given polygon's mode just keep their defaults
// (norm_idx entries at kNoNormal, color left at the neutral gray below).
struct TMD_Polygon {
    static constexpr uint16_t kNoNormal = 0xFFFF;

    int num_verts = 3; // 3 or 4
    bool textured = false;
    bool gouraud = false;   // per-vertex normals/colors vs. one shared value
    bool no_light = false;  // color used as-is, no lighting calculation
    bool double_sided = false;
    bool semi_transparent = false;

    // The GPU ordering-table packet size for this primitive, as read from
    // the file. Unlike ilen (which tmd_writer.cpp can always recompute from
    // however many bytes it just wrote), olen encodes something about the
    // real PS1 GPU's packet format that isn't derivable from our own decoded
    // fields - getting it wrong on a rewritten file is exactly the class of
    // bug that once crashed a real game in this project (the clut_header
    // off-by-4 incident), so the writer always prefers this stored value
    // over guessing. 0 means "no original to copy from" (a primitive built
    // fresh, e.g. by the OBJ import pipeline), in which case the writer
    // falls back to a static table of documented values.
    uint8_t olen = 0;

    uint16_t vert_idx[4] = { 0, 0, 0, 0 };
    uint16_t norm_idx[4] = { kNoNormal, kNoNormal, kNoNormal, kNoNormal };

    // Neutral gray (matches the GTE's 0x80 == 1.0x ambient convention) so an
    // untextured, unlit-data polygon still renders as a plausible flat gray
    // rather than black if a mode/flag combination doesn't supply a color.
    uint8_t color[4][3] = { { 128, 128, 128 }, { 128, 128, 128 }, { 128, 128, 128 }, { 128, 128, 128 } };
    // Whether the file actually stored one color per vertex (true) or a
    // single shared one that DecodePolygon replicated into every slot
    // above (false) - stored explicitly rather than inferred from whether
    // color[] happens to differ per vertex, so an unmodified primitive
    // whose vertices happen to share an identical per-vertex color still
    // round-trips through tmd_writer.cpp byte-for-byte.
    bool color_per_vertex = false;

    uint8_t u[4] = { 0, 0, 0, 0 };
    uint8_t v[4] = { 0, 0, 0, 0 };
    uint16_t cba = 0; // CLUT VRAM address: clutY*64 + clutX/16
    uint16_t tsb = 0; // texpage + semi-transparency rate + color mode
};

// A quad's 4 corners are stored/rendered in triangle-strip order (edge
// 0-1, then the opposite edge 2-3 - see gfx::TmdObjectRenderer's
// GL_TRIANGLE_STRIP), not perimeter/outline order. Any code walking a
// quad's outline (drawing it, or converting to/from a format like OBJ or
// glTF that expects perimeter order) needs this permutation - it's self-
// inverse (applying it twice returns the original order), so the same
// table converts in both directions. Kept in one place after a real bug
// (a self-intersecting "pacman" shape in exported/reimported quads) came
// from two separate hand-written copies of this same table drifting out
// of sync with a third.
constexpr int kQuadPerimeterOrder[4] = { 0, 1, 3, 2 };

struct TMD_Object {
    std::vector<TMD_Vertex> vertices;
    std::vector<TMD_Normal> normals;
    std::vector<TMD_Polygon> polygons;
    int32_t scale = 0; // "apparently unused" per TMD.md; kept only so an unedited file round-trips byte-exact
};

// One loaded .tmd file: an ID'd sequence of independent objects/submodels,
// each with its own vertex/normal/polygon tables. TMD stores no per-object
// placement (that hierarchy lives in separate game-specific data the format
// doesn't define), so every object's vertices are already in whatever local
// space its exporter wrote - the viewer offers manual per-object transform
// controls rather than pretending to know a "correct" layout.
struct TMD_Model {
    std::string filename;
    uint32_t flag = 0; // 1 if pre-processed by GsMapModelingData(), else 0
    std::vector<TMD_Object> objects;
};

} // namespace tmd
