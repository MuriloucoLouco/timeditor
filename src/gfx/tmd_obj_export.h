#pragma once
#include "../core/tmd_format.h"
#include "../core/vram_manager.h"
#include <string>
#include <vector>

namespace gfx::TmdObjExport {

struct Options {
    bool bake_textures = true; // if false, skip baking reference PNGs (use when you already have your own reference texture)
    bool include_colors = true; // write each untextured material's flat color as a vertex color (OBJ's extended "v x y z r g b" line), so re-importing it doesn't come back as placeholder purple
    // Pack every distinct texture tile used into one combined PNG (see
    // texture_atlas.h) instead of writing one PNG per textured material -
    // handy when an external tool handles a single texture more easily
    // than several. Ignored when bake_textures is false.
    bool pack_atlas = false;
};

// Writes <base_name>.obj and <base_name>.mtl (referencing one baked PNG per
// distinct textured material) into `out_folder`, covering exactly the
// objects listed in `object_indices` (each becomes "o Object<index>", so
// tmd_obj_import.cpp can recover which TMD object a re-imported mesh
// belongs to straight from that name). Materials are baked straight from
// VRAMManager's raw buffer via DecodeTexPage, so the preview reflects
// whatever TIMs are actually loaded/positioned in VRAM.
//
// This is a one-way visual reference for external editing, not a lossless
// backup: reimporting via "Import Model" always rebuilds geometry fresh
// from whatever's in the edited file (see tmd_mesh_import.h) rather than
// recovering the original shading mode/texture reference/flags - fix those
// up afterward with the Raw Editor (batch-edit helps when many primitives
// need the same fix).
bool ExportForEditing(const tmd::TMD_Model& model, const std::vector<int>& object_indices,
                      const VRAMManager& vram_manager, const std::string& out_folder, const std::string& base_name,
                      const Options& options);

} // namespace gfx::TmdObjExport
