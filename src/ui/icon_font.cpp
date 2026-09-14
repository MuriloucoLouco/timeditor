#include "icon_font.h"
#include "imgui.h"
#include "IconsFontAwesome6.h"
#include "roboto_medium_compressed.h"
#include "fa_solid_900_compressed.h"

namespace ui {

namespace {

// Only the icons this app actually uses, one [codepoint,codepoint] pair
// each - keeps the merged font atlas small instead of rasterizing Font
// Awesome's full multi-thousand-glyph set. Add a pair here (and the
// matching ICON_FA_* macro at the call site) whenever a new icon is used.
const ImWchar kIconRanges[] = {
    0xe09a, 0xe09a, // ICON_FA_ARROW_UP_FROM_BRACKET - extrude
    0xf030, 0xf030, // ICON_FA_CAMERA - view presets
    0xf047, 0xf047, // ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT - move
    0xf065, 0xf065, // ICON_FA_EXPAND - scale
    0xf0d0, 0xf0d0, // ICON_FA_WAND_MAGIC - recalculate normal
    0xf12d, 0xf12d, // ICON_FA_ERASER - image editor eraser tool
    0xf192, 0xf192, // ICON_FA_CIRCLE_DOT - vertex select
    0xf1b2, 0xf1b2, // ICON_FA_CUBE - perspective view
    0xf1f8, 0xf1f8, // ICON_FA_TRASH - delete
    0xf1fb, 0xf1fb, // ICON_FA_EYE_DROPPER - image editor eyedropper tool
    0xf24d, 0xf24d, // ICON_FA_CLONE - duplicate
    0xf2f1, 0xf2f1, // ICON_FA_ROTATE - rotate
    0xf2f9, 0xf2f9, // ICON_FA_ROTATE_RIGHT - flip normal
    0xf303, 0xf303, // ICON_FA_PENCIL - image editor pencil tool
    0xf387, 0xf387, // ICON_FA_CODE_MERGE - merge
    0xf45c, 0xf45c, // ICON_FA_SQUARE_FULL - side view / image editor rect tool
    0xf51a, 0xf51a, // ICON_FA_BROOM - remove unused
    0xf576, 0xf576, // ICON_FA_FILL_DRIP - image editor fill tool
    0xf5cb, 0xf5cb, // ICON_FA_VECTOR_SQUARE - face select
    0xf5ee, 0xf5ee, // ICON_FA_DRAW_POLYGON - make face
    0xf715, 0xf715, // ICON_FA_SLASH - edge select / image editor line tool
    0xf84c, 0xf84c, // ICON_FA_BORDER_ALL - top view
    0,
};

} // namespace

void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();

    io.Fonts->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data,
                                              static_cast<int>(RobotoMedium_compressed_size), 17.0f);

    ImFontConfig icon_config;
    icon_config.MergeMode = true;
    icon_config.PixelSnapH = true;
    icon_config.GlyphMinAdvanceX = 18.0f; // fixed-width icon column, so icon buttons line up
    io.Fonts->AddFontFromMemoryCompressedTTF(FontAwesomeSolid900_compressed_data,
                                              static_cast<int>(FontAwesomeSolid900_compressed_size), 16.0f,
                                              &icon_config, kIconRanges);

    io.Fonts->Build();
}

} // namespace ui
