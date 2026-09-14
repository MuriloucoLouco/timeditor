#pragma once
#include "../core/tmd_format.h"
#include "tmd_obj_import.h"
#include <string>

// glTF counterpart to tmd_obj_import.cpp's OBJ parsing - same public shape
// (ImportReport, Analyze/Apply), dispatched to from there by file
// extension. Kept as a separate translation unit since it depends on
// tinygltf, unlike the plain-text OBJ path.
namespace gfx::TmdGltfImport {

TmdObjImport::ImportReport Analyze(const std::string& path, const tmd::TMD_Model& model);
TmdObjImport::ImportReport Apply(const std::string& path, tmd::TMD_Model& model);

} // namespace gfx::TmdGltfImport
