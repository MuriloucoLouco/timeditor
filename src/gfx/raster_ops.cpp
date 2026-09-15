#include "raster_ops.h"
#include "image_quantizer.h"
#include <algorithm>
#include <cmath>

namespace gfx {

void PaintMasterPixel(TIM_Image& tim, int px, int py, bool erase, const float color[4]) {
    if (px < 0 || py < 0 || px >= tim.master_width || py >= tim.image_header.height) return;
    size_t p = static_cast<size_t>(py) * tim.master_width + px;

    if (erase) {
        tim.master_rgba[p * 4 + 0] = 0;
        tim.master_rgba[p * 4 + 1] = 0;
        tim.master_rgba[p * 4 + 2] = 0;
        tim.master_rgba[p * 4 + 3] = 0;
        if (!tim.master_index_map.empty()) tim.master_index_map[p] = 0; // index 0: transparent color-key convention
        return;
    }

    uint8_t r = static_cast<uint8_t>(color[0] * 255.0f);
    uint8_t g = static_cast<uint8_t>(color[1] * 255.0f);
    uint8_t b = static_cast<uint8_t>(color[2] * 255.0f);
    uint8_t a = static_cast<uint8_t>(color[3] * 255.0f);
    tim.master_rgba[p * 4 + 0] = r;
    tim.master_rgba[p * 4 + 1] = g;
    tim.master_rgba[p * 4 + 2] = b;
    tim.master_rgba[p * 4 + 3] = a;

    if (!tim.master_index_map.empty()) {
        int colors = tim.clut_header.colors_per_clut;
        auto begin = tim.clut_data.begin() + static_cast<long>(tim.selected_clut) * colors;
        std::vector<uint16_t> row(begin, begin + colors);
        tim.master_index_map[p] = static_cast<uint8_t>(gfx::ImageQuantizer::NearestPaletteIndex(row, r, g, b, a));
    }
}

void DrawLineMaster(TIM_Image& tim, int x0, int y0, int x1, int y1, bool erase, const float color[4]) {
    int dx = std::abs(x1 - x0), sx = x1 >= x0 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y1 >= y0 ? 1 : -1;
    int err = dx + dy;
    int x = x0, y = y0;
    while (true) {
        PaintMasterPixel(tim, x, y, erase, color);
        if (x == x1 && y == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
}

void FillRectMaster(TIM_Image& tim, int x0, int y0, int x1, int y1, bool filled, bool erase, const float color[4]) {
    int lo_x = std::min(x0, x1), hi_x = std::max(x0, x1);
    int lo_y = std::min(y0, y1), hi_y = std::max(y0, y1);
    if (filled) {
        for (int y = lo_y; y <= hi_y; y++) {
            for (int x = lo_x; x <= hi_x; x++) PaintMasterPixel(tim, x, y, erase, color);
        }
    } else {
        for (int x = lo_x; x <= hi_x; x++) {
            PaintMasterPixel(tim, x, lo_y, erase, color);
            PaintMasterPixel(tim, x, hi_y, erase, color);
        }
        for (int y = lo_y; y <= hi_y; y++) {
            PaintMasterPixel(tim, lo_x, y, erase, color);
            PaintMasterPixel(tim, hi_x, y, erase, color);
        }
    }
}

void FloodFillMaster(TIM_Image& tim, int px, int py, const float color[4], const SelectionRect& selection) {
    int w = tim.master_width, h = tim.image_header.height;
    if (px < 0 || py < 0 || px >= w || py >= h) return;

    // An active selection clips the flood fill to its bounds (like any
    // other paint tool is already implicitly bounded by where you click/
    // drag) - flood fill is the one tool that would otherwise ignore it
    // entirely and spread across the whole canvas.
    int lo_x = 0, hi_x = w - 1, lo_y = 0, hi_y = h - 1;
    if (selection.has_selection) {
        lo_x = std::max(0, std::min(selection.x0, selection.x1));
        hi_x = std::min(w - 1, std::max(selection.x0, selection.x1));
        lo_y = std::max(0, std::min(selection.y0, selection.y1));
        hi_y = std::min(h - 1, std::max(selection.y0, selection.y1));
        if (px < lo_x || px > hi_x || py < lo_y || py > hi_y) return;
    }

    size_t start = static_cast<size_t>(py) * w + px;
    uint8_t target[4] = { tim.master_rgba[start * 4 + 0], tim.master_rgba[start * 4 + 1],
                           tim.master_rgba[start * 4 + 2], tim.master_rgba[start * 4 + 3] };
    uint8_t fill[4] = { static_cast<uint8_t>(color[0] * 255.0f), static_cast<uint8_t>(color[1] * 255.0f),
                         static_cast<uint8_t>(color[2] * 255.0f), static_cast<uint8_t>(color[3] * 255.0f) };
    if (target[0] == fill[0] && target[1] == fill[1] && target[2] == fill[2] && target[3] == fill[3]) return;

    std::vector<bool> visited(static_cast<size_t>(w) * h, false);
    std::vector<int> stack;
    stack.push_back(py * w + px);
    visited[start] = true;

    while (!stack.empty()) {
        int idx = stack.back();
        stack.pop_back();
        int x = idx % w, y = idx / w;
        PaintMasterPixel(tim, x, y, false, color);

        int neighbors[4][2] = { { x - 1, y }, { x + 1, y }, { x, y - 1 }, { x, y + 1 } };
        for (auto& n : neighbors) {
            int nx = n[0], ny = n[1];
            if (nx < lo_x || ny < lo_y || nx > hi_x || ny > hi_y) continue;
            size_t ni = static_cast<size_t>(ny) * w + nx;
            if (visited[ni]) continue;
            const uint8_t* c = &tim.master_rgba[ni * 4];
            if (c[0] == target[0] && c[1] == target[1] && c[2] == target[2] && c[3] == target[3]) {
                visited[ni] = true;
                stack.push_back(ny * w + nx);
            }
        }
    }
}

void MoveSelectionMaster(TIM_Image& tim, int dx, int dy, const SelectionRect& selection,
                          const std::vector<uint8_t>& backup_rgba, const std::vector<uint8_t>& backup_index) {
    int w = tim.master_width, h = tim.image_header.height;
    int lo_x = std::min(selection.x0, selection.x1), hi_x = std::max(selection.x0, selection.x1);
    int lo_y = std::min(selection.y0, selection.y1), hi_y = std::max(selection.y0, selection.y1);
    const float kTransparentBlack[4] = { 0, 0, 0, 0 };

    // Cut: clear the selection's original spot (tim's buffers were already
    // reset to the pre-drag backup by the caller this frame, so this only
    // needs to blank the rect itself, not the whole canvas).
    for (int y = lo_y; y <= hi_y; y++) {
        for (int x = lo_x; x <= hi_x; x++) PaintMasterPixel(tim, x, y, /*erase=*/true, kTransparentBlack);
    }

    // Paste: stamp the ORIGINAL content - read from the untouched backup,
    // not from tim itself, since tim's own copy at the source was just
    // cleared above (and could overlap the destination for a small drag).
    for (int y = lo_y; y <= hi_y; y++) {
        int dst_y = y + dy;
        if (dst_y < 0 || dst_y >= h) continue;
        for (int x = lo_x; x <= hi_x; x++) {
            int dst_x = x + dx;
            if (dst_x < 0 || dst_x >= w) continue;
            size_t src_p = static_cast<size_t>(y) * w + x;
            size_t dst_p = static_cast<size_t>(dst_y) * w + dst_x;
            for (int c = 0; c < 4; c++) tim.master_rgba[dst_p * 4 + c] = backup_rgba[src_p * 4 + c];
            if (!tim.master_index_map.empty() && src_p < backup_index.size()) {
                tim.master_index_map[dst_p] = backup_index[src_p];
            }
        }
    }
}

} // namespace gfx
