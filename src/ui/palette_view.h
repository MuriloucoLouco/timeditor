#pragma once
#include "../core/tim_format.h"

namespace ui {

// Draws a grid of color swatches for one CLUT of a TIM. Kept separate from
// InspectorPanel so future features (color editing, picking) can reuse it.
class PaletteView {
public:
    static void Draw(const TIM_Image& tim, int clut_index, float swatch_size = 14.0f);
};

} // namespace ui
