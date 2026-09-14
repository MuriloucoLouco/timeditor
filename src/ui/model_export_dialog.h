#pragma once
#include "../core/tmd_format.h"
#include "../core/vram_manager.h"
#include <string>

namespace ui {

// Modal popup for "Export Model...": bakes reference textures and writes
// an OBJ or glTF bundle for one TMD object or a whole file, ready to edit
// in an external tool like Blender - see gfx::TmdObjExport/TmdGltfExport.
class ModelExportDialog {
public:
    // `active_object` is offered as "This Object" (disabled if < 0).
    void Open(const std::string& tmd_filename, int object_count, int active_object);
    void Render(const tmd::TMD_Model& model, const VRAMManager& vram_manager);

private:
    bool should_open = false;
    int scope = 0;  // 0 = This Object, 1 = Whole File
    int format = 0; // 0 = OBJ, 1 = glTF
    int this_object_index = -1;
    int object_count = 0;
    bool bake_textures = true;
    bool pack_atlas = false;
    bool include_colors = true;
    char output_dir[512] = "";
    char base_name[128] = "";
    std::string status_message;
};

} // namespace ui
