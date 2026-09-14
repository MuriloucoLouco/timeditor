#pragma once
#include "../core/tmd_format.h"
#include "../gfx/tmd_obj_import.h"
#include <string>

namespace ui {

// Modal popup for "Import Model...": picks a .obj or .gltf/.glb (exported
// earlier via "Export Model...", and possibly edited externally since),
// runs a dry-run analysis, and - only once the user confirms - hands the
// picked path and that same report back to the caller so it can push undo
// for each affected object *before* actually calling
// gfx::TmdObjImport::Apply (which this dialog never calls itself). Apply
// always rebuilds each object's geometry entirely fresh from the file -
// see tmd_mesh_import.h for exactly how texture/color/shading get decided.
class ModelImportDialog {
public:
    void Open();

    // Returns true the one frame "Apply" is clicked, filling `out_path`
    // and `out_report` (the already-computed dry-run) for the caller to act on.
    bool Render(const tmd::TMD_Model& model, std::string& out_path, gfx::TmdObjImport::ImportReport& out_report);

private:
    bool should_open = false;
    char model_path[512] = "";
    gfx::TmdObjImport::ImportReport report;
    bool analyzed = false;
};

} // namespace ui
