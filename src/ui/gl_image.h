#pragma once
#include "imgui.h"
#include "../core/gl_compat.h"
#include <cstdint>

namespace ui {

// Draws `tex` as a crisp, pixel-perfect ImGui::Image: binds it with nearest-
// neighbor filtering AND forces the backend's sampler to nearest for this
// one draw. Both are necessary - the backend's default sampler is linear
// and overrides GL_NEAREST set on the texture object alone (see
// ImGuiPlatformIO::DrawCallback_SetSamplerNearest/Linear) - so every TIM/
// VRAM texture in this app should be drawn through this rather than a bare
// ImGui::Image call, or zoomed-in pixel art will blur.
inline void ImagePixelPerfect(uint32_t tex, ImVec2 size) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (platform_io.DrawCallback_SetSamplerNearest) draw_list->AddCallback(platform_io.DrawCallback_SetSamplerNearest);
    ImGui::Image((void*)(intptr_t)tex, size);
    if (platform_io.DrawCallback_SetSamplerLinear) draw_list->AddCallback(platform_io.DrawCallback_SetSamplerLinear);
}

// Same as ImagePixelPerfect, but a clickable ImageButton (returns true on click).
inline bool ImageButtonPixelPerfect(const char* str_id, uint32_t tex, ImVec2 size) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (platform_io.DrawCallback_SetSamplerNearest) draw_list->AddCallback(platform_io.DrawCallback_SetSamplerNearest);
    bool clicked = ImGui::ImageButton(str_id, (void*)(intptr_t)tex, size);
    if (platform_io.DrawCallback_SetSamplerLinear) draw_list->AddCallback(platform_io.DrawCallback_SetSamplerLinear);
    return clicked;
}

} // namespace ui
