#pragma once
// <GL/gl.h> needs WINGDIAPI/APIENTRY already defined before it's included -
// on Windows those macros live in <windows.h>, not in gl.h itself (unlike
// Linux/Mesa's self-contained GL/gl.h), so a bare #include <GL/gl.h> with
// no windows.h in sight compiles fine on Linux but fails on MSVC with a
// wall of "'void' should be preceded by ';'" / "redefinition" errors as it
// tries to parse WINGDIAPI/APIENTRY as plain (undefined) identifiers.
// Every file in this project that touches raw OpenGL 1.x includes this
// instead of <GL/gl.h> directly, so the fix lives in exactly one place.
// Lives in core/ (not gfx/) so core/vram_manager.cpp - the one core file
// that also owns a live GL texture, see docs/ARCHITECTURE.md - can use it
// too without depending on the gfx layer.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX // otherwise windows.h's min/max macros break every std::min/std::max call in this codebase
#endif
#include <windows.h>
#endif
#include <GL/gl.h>

// GL_CLAMP_TO_EDGE is core since OpenGL 1.2, but Windows' own strict GL 1.1-
// only GL/gl.h doesn't declare it (unlike Linux/Mesa's, which does despite
// the same nominal version) - its value is part of the stable core GL enum
// space, safe to hardcode when missing rather than pull in a loader for one
// constant.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// Same story for GL_SHADING_LANGUAGE_VERSION (core since GL 2.0) - used by
// main.cpp's startup GL-info log.
#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif
