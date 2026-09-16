#include "gl_ext.h"
#include "../core/log.h"
#include <GLFW/glfw3.h>

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
    if (!ptr) core::Log::Error("Failed to resolve GL extension function: %s", name);
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
        core::Log::Error("One or more framebuffer-object functions are unavailable - the 3D viewport will not "
                          "render.");
    }
}

namespace {
const char* ErrorName(GLenum err) {
    switch (err) {
        case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
        case GL_STACK_OVERFLOW: return "GL_STACK_OVERFLOW";
        case GL_STACK_UNDERFLOW: return "GL_STACK_UNDERFLOW";
        case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
        default: return "unknown error";
    }
}
} // namespace

void LogGLErrors(const char* where) {
    for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError()) {
        core::Log::Error("GL error at %s: 0x%04X (%s)", where, err, ErrorName(err));
    }
}

} // namespace gfx::gl
