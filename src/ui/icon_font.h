#pragma once

namespace ui {

// Loads the app's base UI font (Roboto Medium, replacing ImGui's default
// bitmap font) plus a small, purpose-picked subset of Font Awesome 6 Free
// Solid icons merged into the same font, so ICON_FA_* string macros from
// third_party/IconFontCppHeaders/IconsFontAwesome6.h can be used inline in
// any ImGui::Text/Button/etc. call. Both fonts are embedded as compressed
// C arrays (see third_party/fonts/) rather than loaded from a file at
// runtime, so there's no asset-deployment path to get wrong on any
// platform. Must be called after ImGui::CreateContext() and before the
// first ImGui::NewFrame().
void LoadFonts();

} // namespace ui
