#pragma once
#include <cstdint>

namespace gfx {

// An offscreen color+depth render target the 3D viewer draws into with
// plain immediate-mode GL, then displays via ImGui::Image - the standard
// way to mix a "real" 3D viewport into an ImGui layout.
class Framebuffer {
public:
    ~Framebuffer();

    // (Re)allocates the FBO's attachments if `width`/`height` changed (or on
    // first use). Safe to call every frame. Logs to stderr (once per size
    // change) if the resulting FBO is incomplete - the viewport would then
    // render as blank with no other visible symptom.
    void EnsureSize(int width, int height);

    void Bind();   // Also sets the GL viewport to the framebuffer's size.
    void Unbind(); // Restores the default framebuffer (0).

    uint32_t ColorTexture() const { return color_tex; }
    int Width() const { return width; }
    int Height() const { return height; }

private:
    uint32_t fbo = 0;
    uint32_t color_tex = 0;
    uint32_t depth_rbo = 0;
    int width = 0;
    int height = 0;

    void Destroy();
};

} // namespace gfx
