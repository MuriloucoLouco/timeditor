#pragma once
#include "imgui.h"

namespace ui {

// The fixed-size icon button every icon toolbar in this app uses (Model
// Editor's Vertex/Edge/Face and Move/Rotate/Scale/view-preset buttons,
// Image Editor's tool palette): a Font Awesome glyph (see icon_font.h) as
// the label, a tooltip on hover, and - while `active` is true - the
// theme's ButtonActive highlight, the "this mode/tool is currently
// selected" convention. Pass `active = false` for a plain one-shot action
// button (Delete/Duplicate/Extrude) that never highlights.
//
// `id_suffix`, when non-null, disambiguates two buttons that would
// otherwise collide on the same ImGui ID - the label, which for an icon
// button is just the glyph itself, so two buttons using the same icon in
// the same toolbar (e.g. two differently-named "Merge" actions both using
// ICON_FA_CODE_MERGE) need one to tell them apart. Omitting it when it's
// actually needed shows as Dear ImGui's "2 visible items with conflicting
// ID" warning and only one of the pair being clickable.
// `tooltip` may be null to skip the hover tooltip entirely (e.g. a self-
// explanatory text-label button, rather than a glyph that needs one).
inline bool IconButton(const char* icon, const char* tooltip, ImVec2 size, bool active = false,
                        const char* id_suffix = nullptr) {
    if (id_suffix) ImGui::PushID(id_suffix);
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
    bool clicked = ImGui::Button(icon, size);
    if (active) ImGui::PopStyleColor();
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    if (id_suffix) ImGui::PopID();
    return clicked;
}

} // namespace ui
