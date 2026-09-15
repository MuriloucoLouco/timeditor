#pragma once
#include "../core/tim_format.h"
#include <cstdint>
#include <vector>

namespace gfx {

// Low-level pixel-buffer operations on a TIM_Image's master (highest-
// fidelity) RGBA/index buffers - pulled out of the Image Editor so they're
// plain, ImGui-free functions instead of private methods reaching into
// ImageEditorPanel's own member state, and so they're directly testable
// (see tests/test_raster_ops.cpp) without a GUI.

// Sets one pixel to `color` (RGBA, 0-1 range each), or clears it to fully
// transparent black when `erase` - and, for an indexed image
// (master_index_map non-empty), re-quantizes to the nearest entry in the
// image's currently selected CLUT row. No-op if (px,py) is out of bounds.
void PaintMasterPixel(TIM_Image& tim, int px, int py, bool erase, const float color[4]);

// Bresenham line of PaintMasterPixel calls from (x0,y0) to (x1,y1) inclusive.
void DrawLineMaster(TIM_Image& tim, int x0, int y0, int x1, int y1, bool erase, const float color[4]);

// Axis-aligned rectangle (corners in either order), filled or outline-only,
// of PaintMasterPixel calls.
void FillRectMaster(TIM_Image& tim, int x0, int y0, int x1, int y1, bool filled, bool erase, const float color[4]);

// The Select tool's active region, if any - FloodFillMaster/
// MoveSelectionMaster both clip to it instead of the whole canvas.
// has_selection = false means "no clip", i.e. the entire image.
struct SelectionRect {
    bool has_selection = false;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // pixel bounds, either corner order
};

// 4-directionally-connected flood fill from (px,py), replacing every
// contiguous same-RGBA-and-alpha pixel with `color` - clipped to
// `selection` when one is active. No-op if the seed pixel already matches
// `color` exactly, or if (px,py) falls outside `selection`.
void FloodFillMaster(TIM_Image& tim, int px, int py, const float color[4], const SelectionRect& selection);

// Cuts `selection`'s pixels out of `backup_rgba`/`backup_index` (a full-
// canvas snapshot taken before the move-drag started - deliberately NOT
// read from `tim`'s own current buffers, which may already be mid-drag)
// and re-stamps them offset by (dx, dy) into `tim`'s live buffers. Meant to
// be called fresh every drag frame; the caller is responsible for having
// already restored `tim`'s buffers from that same backup this frame (see
// image_editor_panel.cpp's HandleToolInput) - this only clears and
// re-stamps the selection rect itself, not the whole canvas.
void MoveSelectionMaster(TIM_Image& tim, int dx, int dy, const SelectionRect& selection,
                          const std::vector<uint8_t>& backup_rgba, const std::vector<uint8_t>& backup_index);

} // namespace gfx
