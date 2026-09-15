// Verifies gfx/raster_ops.h - the Image Editor's low-level pixel-buffer
// operations, extracted so they're plain functions instead of private
// ImageEditorPanel methods reaching into its member state, and so they're
// testable without a GUI.
#include "core/tim_format.h"
#include "gfx/raster_ops.h"
#include <cstdio>

using namespace gfx;

namespace {
int failures = 0;
void Check(bool cond, const char* msg) {
    if (!cond) { printf("FAILED: %s\n", msg); failures++; }
    else printf("OK: %s\n", msg);
}

// A small direct-color (non-indexed) canvas - simplest case, no CLUT
// involved. master_index_map stays empty, matching a 16/24bpp TIM_Image.
TIM_Image MakeDirectCanvas(int w, int h) {
    TIM_Image tim;
    tim.master_width = w;
    tim.image_header.height = static_cast<uint16_t>(h);
    tim.master_rgba.assign(static_cast<size_t>(w) * h * 4, 0);
    return tim;
}

uint8_t PixelR(const TIM_Image& tim, int x, int y) {
    return tim.master_rgba[(static_cast<size_t>(y) * tim.master_width + x) * 4 + 0];
}
uint8_t PixelA(const TIM_Image& tim, int x, int y) {
    return tim.master_rgba[(static_cast<size_t>(y) * tim.master_width + x) * 4 + 3];
}
uint8_t PixelB(const TIM_Image& tim, int x, int y) {
    return tim.master_rgba[(static_cast<size_t>(y) * tim.master_width + x) * 4 + 2];
}
} // namespace

int main() {
    const float kRed[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    const float kBlue[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

    // --- PaintMasterPixel ---
    {
        TIM_Image tim = MakeDirectCanvas(8, 8);
        PaintMasterPixel(tim, 3, 4, false, kRed);
        Check(PixelR(tim, 3, 4) == 255 && PixelA(tim, 3, 4) == 255, "PaintMasterPixel: sets RGBA at the target pixel");
        Check(PixelR(tim, 0, 0) == 0, "PaintMasterPixel: leaves other pixels untouched");

        PaintMasterPixel(tim, 3, 4, true, kRed);
        Check(PixelA(tim, 3, 4) == 0, "PaintMasterPixel: erase clears alpha to 0 (transparent)");

        // Out of bounds must be a silent no-op, not a crash.
        PaintMasterPixel(tim, -1, 0, false, kRed);
        PaintMasterPixel(tim, 0, 99, false, kRed);
        Check(true, "PaintMasterPixel: out-of-bounds coordinates don't crash");
    }

    // --- DrawLineMaster ---
    {
        TIM_Image tim = MakeDirectCanvas(8, 8);
        DrawLineMaster(tim, 0, 0, 7, 0, false, kRed); // a straight horizontal line
        bool all_set = true;
        for (int x = 0; x < 8; x++) {
            if (PixelA(tim, x, 0) != 255) all_set = false;
        }
        Check(all_set, "DrawLineMaster: every pixel along a straight horizontal line is painted");
        Check(PixelA(tim, 0, 1) == 0, "DrawLineMaster: the row below the line is untouched");
    }

    // --- FillRectMaster ---
    {
        TIM_Image tim = MakeDirectCanvas(8, 8);
        FillRectMaster(tim, 2, 2, 5, 5, /*filled=*/true, false, kRed);
        int painted = 0;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                if (PixelA(tim, x, y) == 255) painted++;
            }
        }
        Check(painted == 16, "FillRectMaster(filled): a 4x4 rect paints exactly 16 pixels");

        TIM_Image tim2 = MakeDirectCanvas(8, 8);
        FillRectMaster(tim2, 2, 2, 5, 5, /*filled=*/false, false, kRed);
        int outline_painted = 0;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                if (PixelA(tim2, x, y) == 255) outline_painted++;
            }
        }
        // A 4x4 outline: 4 corners counted once each, 2*(4-2) edge pixels
        // per side not double-counted - perimeter of a 4x4 square is 12.
        Check(outline_painted == 12, "FillRectMaster(outline): a 4x4 rect's outline paints exactly its 12-pixel perimeter");
        Check(PixelA(tim2, 3, 3) == 0, "FillRectMaster(outline): the interior stays untouched");
    }

    // --- FloodFillMaster ---
    {
        TIM_Image tim = MakeDirectCanvas(8, 8);
        // A 3x3 red block in the corner; flood-filling from inside it with
        // blue should replace exactly those 9 pixels, not spill past them
        // (the rest of the canvas is still transparent black - a different
        // color - so the flood naturally stops at the block's edge).
        FillRectMaster(tim, 0, 0, 2, 2, true, false, kRed);
        FloodFillMaster(tim, 1, 1, kBlue, SelectionRect{});
        int blue_count = 0, red_count = 0;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                if (PixelR(tim, x, y) == 0 && PixelB(tim, x, y) == 255) blue_count++;
                if (PixelR(tim, x, y) == 255) red_count++;
            }
        }
        Check(blue_count == 9 && red_count == 0, "FloodFillMaster: replaces the whole contiguous red block with blue");

        // No-op when the seed pixel already matches the fill color exactly.
        TIM_Image tim2 = MakeDirectCanvas(4, 4);
        FillRectMaster(tim2, 0, 0, 3, 3, true, false, kRed);
        FloodFillMaster(tim2, 0, 0, kRed, SelectionRect{});
        Check(PixelR(tim2, 0, 0) == 255, "FloodFillMaster: no-op when seed already matches the fill color");

        // Clipped to an active selection - filling from inside a red block
        // that extends *past* the selection must not touch pixels outside it.
        TIM_Image tim3 = MakeDirectCanvas(8, 8);
        FillRectMaster(tim3, 0, 0, 7, 7, true, false, kRed); // whole canvas red
        SelectionRect sel{ true, 0, 0, 3, 3 }; // top-left 4x4 only
        FloodFillMaster(tim3, 1, 1, kBlue, sel);
        Check(PixelR(tim3, 4, 4) == 255,
              "FloodFillMaster: a pixel outside the selection stays red (fill didn't leak past the clip)");
        Check(PixelR(tim3, 1, 1) == 0 && PixelB(tim3, 1, 1) == 255,
              "FloodFillMaster: a pixel inside the selection was actually filled blue");
    }

    // --- MoveSelectionMaster ---
    {
        TIM_Image tim = MakeDirectCanvas(8, 8);
        FillRectMaster(tim, 1, 1, 2, 2, true, false, kRed); // a 2x2 red block at (1,1)-(2,2)
        std::vector<uint8_t> backup_rgba = tim.master_rgba;
        std::vector<uint8_t> backup_index; // empty: direct-color canvas, no index map
        SelectionRect sel{ true, 1, 1, 2, 2 };

        MoveSelectionMaster(tim, /*dx=*/3, /*dy=*/0, sel, backup_rgba, backup_index);

        Check(PixelA(tim, 1, 1) == 0 && PixelA(tim, 2, 2) == 0,
              "MoveSelectionMaster: the original selection area is cleared (cut)");
        Check(PixelR(tim, 4, 1) == 255 && PixelR(tim, 5, 2) == 255,
              "MoveSelectionMaster: the content reappears at the destination offset (paste)");
    }

    printf(failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d TEST(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
