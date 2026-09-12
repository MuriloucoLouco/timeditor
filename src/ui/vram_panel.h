#pragma once
#include "../core/vram_manager.h"
#include "imgui.h"

namespace ui {

// Aba "VRAM Viewer": mostra o conteúdo emulado da VRAM com a grade de
// tpages sobreposta, em diferentes modos de BPP.
class VRAMPanel {
public:
    void Render(VRAMManager& vram_manager);

private:
    int bpp_mode_index = 2; // Índice: 0 = 4 BPP, 1 = 8 BPP, 2 = 16 BPP
    float zoom = 1.0f;

    static VRAMViewMode IndexToViewMode(int index);
    static int TPageWidthPixelsForMode(VRAMViewMode mode);
    void DrawTPageGrid(ImDrawList* draw_list, ImVec2 origin, float tpage_w, float tpage_h) const;
};

} // namespace ui
