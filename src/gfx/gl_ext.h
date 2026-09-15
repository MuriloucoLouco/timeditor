#pragma once
#include "../core/gl_compat.h"

// Framebuffer-object entry points. <GL/gl.h> on Linux only declares GL 1.1,
// but FBOs (core since GL 3.0 / universally available as an extension
// before that) are needed to render the 3D viewer offscreen into a texture
// ImGui can display. Rather than pull in a full loader (GLAD/GLEW) for
// just these few functions, they're resolved by hand via glfwGetProcAddress
// - see LoadGLExtensions(), called once during EditorApp::Initialize().
namespace gfx::gl {

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif

using PFNGLGENFRAMEBUFFERSPROC = void (*)(GLsizei, GLuint*);
using PFNGLBINDFRAMEBUFFERPROC = void (*)(GLenum, GLuint);
using PFNGLFRAMEBUFFERTEXTURE2DPROC = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);
using PFNGLGENRENDERBUFFERSPROC = void (*)(GLsizei, GLuint*);
using PFNGLBINDRENDERBUFFERPROC = void (*)(GLenum, GLuint);
using PFNGLRENDERBUFFERSTORAGEPROC = void (*)(GLenum, GLenum, GLsizei, GLsizei);
using PFNGLFRAMEBUFFERRENDERBUFFERPROC = void (*)(GLenum, GLenum, GLenum, GLuint);
using PFNGLCHECKFRAMEBUFFERSTATUSPROC = GLenum (*)(GLenum);
using PFNGLDELETEFRAMEBUFFERSPROC = void (*)(GLsizei, const GLuint*);
using PFNGLDELETERENDERBUFFERSPROC = void (*)(GLsizei, const GLuint*);

extern PFNGLGENFRAMEBUFFERSPROC GenFramebuffers;
extern PFNGLBINDFRAMEBUFFERPROC BindFramebuffer;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D;
extern PFNGLGENRENDERBUFFERSPROC GenRenderbuffers;
extern PFNGLBINDRENDERBUFFERPROC BindRenderbuffer;
extern PFNGLRENDERBUFFERSTORAGEPROC RenderbufferStorage;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC FramebufferRenderbuffer;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus;
extern PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers;
extern PFNGLDELETERENDERBUFFERSPROC DeleteRenderbuffers;

// Resolves all of the above via glfwGetProcAddress. Must be called once,
// after glfwMakeContextCurrent, before any Framebuffer is used.
void LoadGLExtensions();

} // namespace gfx::gl
