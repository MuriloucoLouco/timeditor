#include "gl_ext.h"
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

void LoadGLExtensions() {
    GenFramebuffers = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(glfwGetProcAddress("glGenFramebuffers"));
    BindFramebuffer = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(glfwGetProcAddress("glBindFramebuffer"));
    FramebufferTexture2D =
        reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(glfwGetProcAddress("glFramebufferTexture2D"));
    GenRenderbuffers = reinterpret_cast<PFNGLGENRENDERBUFFERSPROC>(glfwGetProcAddress("glGenRenderbuffers"));
    BindRenderbuffer = reinterpret_cast<PFNGLBINDRENDERBUFFERPROC>(glfwGetProcAddress("glBindRenderbuffer"));
    RenderbufferStorage =
        reinterpret_cast<PFNGLRENDERBUFFERSTORAGEPROC>(glfwGetProcAddress("glRenderbufferStorage"));
    FramebufferRenderbuffer =
        reinterpret_cast<PFNGLFRAMEBUFFERRENDERBUFFERPROC>(glfwGetProcAddress("glFramebufferRenderbuffer"));
    CheckFramebufferStatus =
        reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(glfwGetProcAddress("glCheckFramebufferStatus"));
    DeleteFramebuffers = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(glfwGetProcAddress("glDeleteFramebuffers"));
    DeleteRenderbuffers =
        reinterpret_cast<PFNGLDELETERENDERBUFFERSPROC>(glfwGetProcAddress("glDeleteRenderbuffers"));
}

} // namespace gfx::gl
