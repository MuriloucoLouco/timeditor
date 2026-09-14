#pragma once
#include "../core/tmd_format.h"
#include "../core/vram_manager.h"
#include <string>
#include <vector>

namespace gfx::TmdGltfExport {

struct Options {
    bool bake_textures = true;
    bool include_colors = true;
    // Pack every distinct texture tile used into one combined PNG (see
    // texture_atlas.h) instead of writing one PNG per textured material,
    // and point every material's baseColorTexture at it. Ignored when
    // bake_textures is false.
    bool pack_atlas = false;
};

// glTF counterpart to gfx::TmdObjExport::ExportForEditing - same shape and
// same object-naming convention ("Object<index>", as a glTF node name this
// time), writing <base_name>.gltf + <base_name>.bin + one baked PNG per
// textured material into `out_folder`.
bool Export(const tmd::TMD_Model& model, const std::vector<int>& object_indices, const VRAMManager& vram_manager,
            const std::string& out_folder, const std::string& base_name, const Options& options);

} // namespace gfx::TmdGltfExport
