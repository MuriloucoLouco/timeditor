// Headless verification that ui::LoadFonts() actually produces a built
// font atlas with the icon glyphs present - building a font atlas needs
// no window/GL context (Build() just rasterizes to a CPU-side buffer), so
// this can run without any GUI automation.
#include "imgui.h"
#include "ui/icon_font.h"
#include <cstdio>

int main() {
    int failures = 0;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ui::LoadFonts();

    ImGuiIO& io = ImGui::GetIO();
    if (!io.Fonts->IsBuilt()) { printf("FAILED: font atlas did not build\n"); failures++; }
    else printf("OK: font atlas built\n");

    if (io.Fonts->Fonts.Size != 1) {
        printf("FAILED: expected 1 merged ImFont, got %d\n", io.Fonts->Fonts.Size);
        failures++;
    } else printf("OK: base font + icon font merged into a single ImFont\n");

    ImFont* font = io.Fonts->Fonts[0];
    // A handful of the icon codepoints actually used by the toolbars -
    // confirms the compressed FA6 data decompressed, parsed as a valid
    // TTF, and the requested glyph range was actually rasterized.
    struct { ImWchar cp; const char* name; } checks[] = {
        { 0xf192, "ICON_FA_CIRCLE_DOT (vertex select)" },
        { 0xf5cb, "ICON_FA_VECTOR_SQUARE (face select)" },
        { 0xf047, "ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT (move)" },
        { 0xf2f1, "ICON_FA_ROTATE (rotate)" },
        { 0xf1f8, "ICON_FA_TRASH (delete)" },
        { 0xf387, "ICON_FA_CODE_MERGE (merge)" },
    };
    for (auto& c : checks) {
        if (!font->IsGlyphInFont(c.cp)) { printf("FAILED: glyph missing for %s (U+%04X)\n", c.name, c.cp); failures++; }
        else printf("OK: glyph present for %s\n", c.name);
    }

    // Ordinary text glyph from the base font should still work.
    if (!font->IsGlyphInFont('A')) { printf("FAILED: base font glyph 'A' missing\n"); failures++; }
    else printf("OK: base font text glyph 'A' present\n");

    ImGui::DestroyContext();
    printf(failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d TEST(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
