#include "gl_ext.h"
#include <GLFW/glfw3.h>
#include <cstdio>

namespace gfx::gl {

PFNGLGENFRAMEBUFFERSPROC GenFramebuffers = nullptr;
PFNGLBINDFRAMEBUFFERPROC BindFramebuffer = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D = nullptr;
PFNGLGENRENDERBUFFERSPROC GenRenderbuffers = nullptr;
PFNGLBINDRENDERBUFFERPROC BindRenderbuffer = nullptr;
PFNGLRENDERBUFFERSTORAGEPROC RenderbufferStorage = nullptr;
PFNGLFRAMEBUFFERRENDERBUFFERPROC FramebufferRenderbuffer = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers = nullptr;
PFNGLDELETERENDERBUFFERSPROC DeleteRenderbuffers = nullptr;

namespace {
// Logs which FBO entry point failed to resolve - a driver missing any of
// these (e.g. a software/virtualized GL stuck on the 1.1 baseline) leaves
// the 3D viewport rendering into a framebuffer that silently does nothing,
// with no crash to point at the cause.
template <typename ProcT>
ProcT Load(const char* name) {
    ProcT ptr = reinterpret_cast<ProcT>(glfwGetProcAddress(name));
    if (!ptr) std::fprintf(stderr, "[GL] Failed to resolve extension function: %s\n", name);
    return ptr;
}
} // namespace

void LoadGLExtensions() {
    GenFramebuffers = Load<PFNGLGENFRAMEBUFFERSPROC>("glGenFramebuffers");
    BindFramebuffer = Load<PFNGLBINDFRAMEBUFFERPROC>("glBindFramebuffer");
    FramebufferTexture2D = Load<PFNGLFRAMEBUFFERTEXTURE2DPROC>("glFramebufferTexture2D");
    GenRenderbuffers = Load<PFNGLGENRENDERBUFFERSPROC>("glGenRenderbuffers");
    BindRenderbuffer = Load<PFNGLBINDRENDERBUFFERPROC>("glBindRenderbuffer");
    RenderbufferStorage = Load<PFNGLRENDERBUFFERSTORAGEPROC>("glRenderbufferStorage");
    FramebufferRenderbuffer = Load<PFNGLFRAMEBUFFERRENDERBUFFERPROC>("glFramebufferRenderbuffer");
    CheckFramebufferStatus = Load<PFNGLCHECKFRAMEBUFFERSTATUSPROC>("glCheckFramebufferStatus");
    DeleteFramebuffers = Load<PFNGLDELETEFRAMEBUFFERSPROC>("glDeleteFramebuffers");
    DeleteRenderbuffers = Load<PFNGLDELETERENDERBUFFERSPROC>("glDeleteRenderbuffers");

    if (!GenFramebuffers || !BindFramebuffer || !FramebufferTexture2D || !GenRenderbuffers ||
        !BindRenderbuffer || !RenderbufferStorage || !FramebufferRenderbuffer || !CheckFramebufferStatus ||
        !DeleteFramebuffers || !DeleteRenderbuffers) {
        std::fprintf(stderr,
                      "[GL] One or more framebuffer-object functions are unavailable - the 3D viewport "
                      "will not render.\n");
    }
}

} // namespace gfx::gl
