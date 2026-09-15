#pragma once
#include "../core/tmd_format.h"
#include "math3d.h"

namespace gfx {

// PS-X object/normal space is Y-down; every viewer/renderer and every
// export path negates Y once so a conventional Y-up camera/DCC tool shows
// the model upright - and every reimport path negates it back on the way
// in. This one negation was previously copy-pasted (all functionally
// identical) into tmd_panel.cpp, tmd_raw_editor.cpp, model_editor_panel.cpp,
// tmd_object_renderer.cpp, tmd_obj_export.cpp, and tmd_gltf_export.cpp.
inline Vec3 ToViewerSpace(int16_t x, int16_t y, int16_t z) {
    return { static_cast<float>(x), -static_cast<float>(y), static_cast<float>(z) };
}
inline Vec3 ToViewerSpace(const tmd::TMD_Vertex& v) { return ToViewerSpace(v.x, v.y, v.z); }
inline Vec3 ToViewerSpace(const tmd::TMD_Normal& n) { return ToViewerSpace(n.x, n.y, n.z); }

// Same conversion, named for the export direction (identical formula - Y
// negation is its own inverse) so call sites read as what they mean.
inline Vec3 ToExportSpace(const tmd::TMD_Vertex& v) { return ToViewerSpace(v.x, v.y, v.z); }
inline Vec3 ToExportSpace(const tmd::TMD_Normal& n) { return ToViewerSpace(n.x, n.y, n.z); }

} // namespace gfx
