#include "framebuffer.h"
#include "gl_ext.h"
#include <GL/gl.h>

namespace gfx {

Framebuffer::~Framebuffer() { Destroy(); }

void Framebuffer::Destroy() {
    if (fbo) gl::DeleteFramebuffers(1, &fbo);
    if (color_tex) glDeleteTextures(1, &color_tex);
    if (depth_rbo) gl::DeleteRenderbuffers(1, &depth_rbo);
    fbo = color_tex = depth_rbo = 0;
}

void Framebuffer::EnsureSize(int new_width, int new_height) {
    new_width = new_width < 1 ? 1 : new_width;
    new_height = new_height < 1 ? 1 : new_height;
    if (fbo != 0 && new_width == width && new_height == height) return;

    Destroy();
    width = new_width;
    height = new_height;

    glGenTextures(1, &color_tex);
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    gl::GenRenderbuffers(1, &depth_rbo);
    gl::BindRenderbuffer(GL_RENDERBUFFER, depth_rbo);
    gl::RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

    gl::GenFramebuffers(1, &fbo);
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex, 0);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rbo);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::Bind() {
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
}

void Framebuffer::Unbind() {
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace gfx
