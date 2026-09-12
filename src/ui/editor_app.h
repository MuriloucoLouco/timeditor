#pragma once
#include <vector>
#include <string>
#include "../core/tim_format.h"
#include "../core/vram_manager.h"
#include "inspector_panel.h"
#include "vram_panel.h"

namespace ui {

// Ponto central da aplicação: dono dos dados (TIMs carregadas + VRAM) e
// responsável pelo menu principal. A renderização de cada aba é delegada
// aos painéis (InspectorPanel / VRAMPanel).
class EditorApp {
public:
    EditorApp() = default;

    void Initialize();
    void RenderFrame();
    void LoadFile(const std::string& path);

private:
    std::vector<TIM_Image> loaded_tims;
    VRAMManager vram_manager;
    InspectorPanel inspector_panel;
    VRAMPanel vram_panel;

    void RenderMenu();
    void RenderWorkspace();
};

} // namespace ui
