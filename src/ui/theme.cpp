#include "theme.h"
#include "imgui.h"

namespace ui {

void ApplyDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    // A single accent color family (blue) used consistently for every
    // interactive/"this is active" signal - buttons, checkmarks, sliders,
    // the selected tab, focus/nav outlines - rather than each widget kind
    // picking its own color, which is what made every panel read as
    // disconnected default-ImGui pieces rather than one designed app.
    const ImVec4 kAccent(0.24f, 0.48f, 0.75f, 1.00f);
    const ImVec4 kAccentHover(0.32f, 0.58f, 0.85f, 1.00f);
    const ImVec4 kAccentActive(0.18f, 0.38f, 0.62f, 1.00f);
    const ImVec4 kAccentDim(0.24f, 0.48f, 0.75f, 0.35f);

    const ImVec4 kBg(0.11f, 0.12f, 0.14f, 1.00f);
    const ImVec4 kBgLight(0.16f, 0.17f, 0.20f, 1.00f);
    const ImVec4 kBgDark(0.08f, 0.08f, 0.10f, 1.00f);
    const ImVec4 kBorder(0.26f, 0.28f, 0.32f, 0.60f);
    const ImVec4 kText(0.90f, 0.91f, 0.93f, 1.00f);
    const ImVec4 kTextDim(0.52f, 0.54f, 0.58f, 1.00f);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kTextDim;
    c[ImGuiCol_WindowBg] = kBg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(kBgDark.x, kBgDark.y, kBgDark.z, 0.98f);
    c[ImGuiCol_Border] = kBorder;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg] = kBgLight;
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.23f, 0.26f, 0.30f, 1.00f);

    c[ImGuiCol_TitleBg] = kBgDark;
    c[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.15f, 0.19f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed] = kBgDark;
    c[ImGuiCol_MenuBarBg] = kBgDark;

    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.30f, 0.34f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.36f, 0.38f, 0.43f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive] = kAccent;

    c[ImGuiCol_CheckMark] = kAccentHover;
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = kAccentHover;

    c[ImGuiCol_Button] = kAccent;
    c[ImGuiCol_ButtonHovered] = kAccentHover;
    c[ImGuiCol_ButtonActive] = kAccentActive;

    c[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.30f, 1.00f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.32f, 0.38f, 1.00f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.38f, 0.45f, 1.00f);

    c[ImGuiCol_Separator] = kBorder;
    c[ImGuiCol_SeparatorHovered] = kAccentHover;
    c[ImGuiCol_SeparatorActive] = kAccent;

    c[ImGuiCol_ResizeGrip] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.20f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f);
    c[ImGuiCol_ResizeGripActive] = kAccent;

    c[ImGuiCol_Tab] = ImVec4(0.14f, 0.15f, 0.18f, 1.00f);
    c[ImGuiCol_TabHovered] = kAccentHover;
    c[ImGuiCol_TabSelected] = ImVec4(0.19f, 0.23f, 0.29f, 1.00f);
    c[ImGuiCol_TabSelectedOverline] = kAccent;
    c[ImGuiCol_TabDimmed] = kBgDark;
    c[ImGuiCol_TabDimmedSelected] = ImVec4(0.15f, 0.17f, 0.20f, 1.00f);
    c[ImGuiCol_TabDimmedSelectedOverline] = kAccentDim;

    c[ImGuiCol_TableHeaderBg] = kBgLight;
    c[ImGuiCol_TableBorderStrong] = kBorder;
    c[ImGuiCol_TableBorderLight] = ImVec4(kBorder.x, kBorder.y, kBorder.z, 0.35f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.02f);

    c[ImGuiCol_TextSelectedBg] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
    c[ImGuiCol_TextLink] = kAccentHover;
    c[ImGuiCol_DragDropTarget] = ImVec4(0.95f, 0.75f, 0.20f, 0.90f);
    c[ImGuiCol_NavCursor] = kAccent;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(kText.x, kText.y, kText.z, 0.70f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.2f, 0.2f, 0.2f, 0.30f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.05f, 0.05f, 0.06f, 0.55f);

    // Spacing/padding/border/rounding - previously 100% untouched ImGui
    // defaults, which is most of why every panel read as generic rather
    // than deliberately designed regardless of the color palette above.
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 10.0f;
    style.CellPadding = ImVec2(6.0f, 4.0f);

    style.WindowRounding = 0.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
}

} // namespace ui
