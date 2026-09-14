#pragma once
#include "../core/tmd_format.h"
#include "../core/vram_manager.h"
#include "math3d.h"
#include "tmd_texture_cache.h"
#include <vector>

namespace gfx::TmdObjectRenderer {

struct Options {
    bool textured = true;
    bool cull_backfaces = false;
    bool wireframe = false; // GL_LINE polygon mode instead of filled
    // Polygon index (within `obj`) to draw as a solid, unlit, untextured
    // green regardless of its own texture/shading - used by the Raw
    // Editor's primitive preview to pick one polygon out of the whole
    // object. -1 draws every polygon normally.
    int highlight_primitive = -1;
    // Parallel to obj.polygons: true tints that face toward orange (mixed
    // with its own real shading/texture, not a flat override, so a real
    // editor's face selection stays legible without hiding what the face
    // actually looks like) - nullptr means no selection tinting at all.
    // Ignored for the single `highlight_primitive` polygon, if any.
    const std::vector<bool>* selected_polygons = nullptr;
};

// Draws every polygon of `obj` under `model_matrix`, assuming the caller
// has already set up GL_PROJECTION/GL_MODELVIEW (the camera) and bound the
// target framebuffer. Shared by the main 3D viewport (tmd_panel.cpp) and
// the Raw Editor's per-primitive preview (tmd_raw_editor.cpp) so lighting/
// texturing/blending never drifts between the two.
void DrawObject(const tmd::TMD_Object& obj, const Mat4& model_matrix, const VRAMManager& vram_manager,
                 TmdTextureCache& texture_cache, const Options& options);

// One vertex-handle dot or edge line for DrawSelectionOverlay, already in
// object-local space (the same space DrawObject's own per-vertex positions
// are in, before `model_matrix`) - deliberately decoupled from
// tmd::TMD_Object itself (no vertex/edge index bookkeeping here) so the
// Model Editor, which already has to compute these positions for its own
// picking math, can hand them over directly without the renderer needing
// to know anything about selection state or how edges are derived.
struct OverlayPoint {
    Vec3 position;
    bool selected = false;
};
struct OverlayLine {
    Vec3 a, b;
    bool selected = false;
};

// Draws a lightweight vertex/edge overlay on top of whatever was already
// rendered under the same `model_matrix` - unselected dark dots/dim white
// lines, selected orange - for the Model Editor's Vertex/Edge selection
// modes (vertices and edges have no visual representation of their own in
// a plain DrawObject call). Depth-tested against existing geometry (so a
// handle on the far side of the model is naturally hidden) but doesn't
// write depth itself, so it can't interfere with anything drawn after it.
void DrawSelectionOverlay(const Mat4& model_matrix, const std::vector<OverlayPoint>& points,
                          const std::vector<OverlayLine>& lines);

} // namespace gfx::TmdObjectRenderer
