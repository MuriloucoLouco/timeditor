#pragma once
#include <vector>
#include "../core/tim_format.h"

namespace ui {

// Aba "TIM Inspector": lista lateral de arquivos carregados + preview
// ampliado da imagem selecionada (com seletor de CLUT quando aplicável).
class InspectorPanel {
public:
    void Render(std::vector<TIM_Image>& tims);

private:
    int selected_index = -1;
    float zoom_level = 2.0f;

    void RenderFileList(std::vector<TIM_Image>& tims);
    void RenderPreview(TIM_Image& tim);
    void RenderClutSelector(TIM_Image& tim);
};

} // namespace ui
