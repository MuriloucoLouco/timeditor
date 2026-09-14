#pragma once
#include "../core/tmd_format.h"
#include "tmd_mesh_import.h"
#include <string>
#include <vector>

namespace gfx::TmdObjImport {

struct ImportReport {
    bool ok = false;
    std::string error; // set when ok is false (couldn't open/parse the file)
    std::vector<MeshImportStats> objects;
};

// Parses `model_path` (.obj, or .gltf/.glb - dispatched by extension, the
// latter via tmd_gltf_import.cpp) against the currently loaded `model`,
// without changing anything - for showing a dry-run summary before Apply().
ImportReport Analyze(const std::string& model_path, const tmd::TMD_Model& model);

// Re-parses and rebuilds every object the file contains that also exists in
// `model`, replacing that object's entire vertex/normal/primitive tables
// from the (possibly restructured) mesh. Matched primarily by each file's
// "o ObjectN" object/node name (see tmd_obj_export.cpp/tmd_gltf_export.cpp);
// a group whose name doesn't parse (renamed, merged, or missing outright)
// falls back to a shape-based match against whichever TMD objects aren't
// already claimed by name - see tmd_mesh_import.h's ResolveUnmatchedObjects
// - and is only left unmatched (skipped) if nothing confidently fits. See
// tmd_mesh_import.h's RebuildTmdObject for exactly how each face's
// texture/color/shading gets decided.
ImportReport Apply(const std::string& model_path, tmd::TMD_Model& model);

} // namespace gfx::TmdObjImport
