#pragma once
#include "core/tmd_format.h"
#include "core/tmd_mesh_ops.h"

// A small, varied TMD_Model used by the OBJ/glTF export-reimport round-trip
// tests: a mix of textured/flat, tri/quad, and gouraud/no_light/
// double_sided/semi_transparent/color_per_vertex combinations, so a
// round-trip bug that only shows up for one specific mode doesn't slip
// through. Built via tmd::AddFace/AddVertex (so vertex winding and normals
// are already correct) then mutated with arbitrary-but-distinct texture/UV/
// color/flag values per primitive - exact geometric "correctness" of those
// values doesn't matter for a round-trip fidelity check, only that they're
// distinct and preserved. Deliberately doesn't need a real .tmd file (which
// this repo can't commit) or any VRAM content (export with bake_textures =
// false never looks at actual texel data, only at tsb/cba's bits).
inline tmd::TMD_Model BuildSyntheticTmdModel() {
    tmd::TMD_Model model;
    tmd::TMD_Object obj;

    // 8 corners of a small cube, +-50 units - see tests/test_mesh_ops.cpp's
    // MakeCube for the same bit-indexed corner convention.
    for (int i = 0; i < 8; i++) {
        float x = (i & 1) ? 50.0f : -50.0f;
        float y = (i & 2) ? 50.0f : -50.0f;
        float z = (i & 4) ? 50.0f : -50.0f;
        tmd::AddVertex(obj, x, y, z);
    }

    // Flat, unlit quad.
    int f0 = tmd::AddFace(obj, { 0, 1, 3, 2 });
    obj.polygons[f0].no_light = true;
    obj.polygons[f0].color[0][0] = 200;
    obj.polygons[f0].color[0][1] = 40;
    obj.polygons[f0].color[0][2] = 40;

    // Gouraud-shaded, per-vertex-colored, double-sided quad.
    int f1 = tmd::AddFace(obj, { 4, 5, 7, 6 });
    obj.polygons[f1].gouraud = true;
    obj.polygons[f1].double_sided = true;
    obj.polygons[f1].color_per_vertex = true;
    for (int k = 0; k < 4; k++) {
        obj.polygons[f1].color[k][0] = static_cast<uint8_t>(20 * (k + 1));
        obj.polygons[f1].color[k][1] = static_cast<uint8_t>(30 * (k + 1));
        obj.polygons[f1].color[k][2] = static_cast<uint8_t>(10 * (k + 1));
    }

    // Flat, semi-transparent triangle.
    int f2 = tmd::AddFace(obj, { 0, 1, 5 });
    obj.polygons[f2].semi_transparent = true;
    obj.polygons[f2].color[0][0] = 80;
    obj.polygons[f2].color[0][1] = 160;
    obj.polygons[f2].color[0][2] = 240;

    // Textured quad, 4bpp tile (tsb color-mode bits = 0, 256-wide), distinct
    // UVs per corner. Deliberately NOT an 8/16bpp (narrower) tile: OBJ
    // export divides U by the primitive's *actual* tile width, but reimport
    // always assumes 256-wide/4bpp (freshly-imported primitives always
    // reset to tsb=0 - see test_obj_roundtrip.cpp's comment) - so a
    // narrower original tile's UVs would come back scaled by design, not by
    // bug, which isn't what this fidelity check is testing for.
    int f3 = tmd::AddFace(obj, { 0, 2, 6, 4 });
    obj.polygons[f3].textured = true;
    obj.polygons[f3].tsb = 0;
    obj.polygons[f3].cba = 640;
    const uint8_t u3[4] = { 10, 120, 127, 5 };
    const uint8_t v3[4] = { 3, 8, 200, 250 };
    for (int k = 0; k < 4; k++) {
        obj.polygons[f3].u[k] = u3[k];
        obj.polygons[f3].v[k] = v3[k];
    }

    // Textured triangle, 4bpp tile (tsb color-mode bits = 0).
    int f4 = tmd::AddFace(obj, { 1, 3, 7 });
    obj.polygons[f4].textured = true;
    obj.polygons[f4].tsb = 0;
    obj.polygons[f4].cba = 128;
    const uint8_t u4[3] = { 0, 255, 60 };
    const uint8_t v4[3] = { 0, 40, 255 };
    for (int k = 0; k < 3; k++) {
        obj.polygons[f4].u[k] = u4[k];
        obj.polygons[f4].v[k] = v4[k];
    }

    model.objects.push_back(std::move(obj));
    return model;
}
