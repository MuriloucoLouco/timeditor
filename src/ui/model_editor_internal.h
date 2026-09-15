#pragma once
#include "../core/tmd_format.h"
#include "../gfx/math3d.h"
#include "../gfx/tmd_space.h"

// Implementation-detail helpers shared across ModelEditorPanel's several
// .cpp files (model_editor_panel.cpp, model_editor_input.cpp,
// model_editor_uv.cpp - the class itself is split across all three purely
// to keep any one file from growing unmanageably long, see
// docs/ARCHITECTURE.md). Not part of the public class interface, so this
// header isn't included by model_editor_panel.h and nothing outside these
// three .cpp files should need it.
namespace ui::model_editor_internal {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

inline gfx::Vec3 VertexViewerPos(const tmd::TMD_Object& obj, int vi) {
    return gfx::ToViewerSpace(obj.vertices[vi]);
}

} // namespace ui::model_editor_internal
