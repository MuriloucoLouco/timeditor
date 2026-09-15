cmake_minimum_required(VERSION 3.15)
project(TIMEditor)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

# Absolute (not source-dir-relative) so tests/CMakeLists.txt - a different
# source directory - can also reference these paths unambiguously.
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(STB_DIR ${CMAKE_SOURCE_DIR}/third_party/stb)
set(TINYGLTF_DIR ${CMAKE_SOURCE_DIR}/third_party/tinygltf)
set(IMGUIZMO_DIR ${CMAKE_SOURCE_DIR}/third_party/ImGuizmo/src) # tag 1.10+ moved ImGuizmo.h/.cpp under src/
set(FONTS_DIR ${CMAKE_SOURCE_DIR}/third_party/fonts)
set(ICONFONTCPPHEADERS_DIR ${CMAKE_SOURCE_DIR}/third_party/IconFontCppHeaders)

# Core ImGui only (no demo, no backends) - what a headless target (like
# tests/test_fonts.cpp, which just builds a font atlas) needs; the full app
# additionally needs the demo window and the GLFW/OpenGL3 backends.
set(IMGUI_CORE_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)
set(IMGUI_SOURCES
    ${IMGUI_CORE_SOURCES}
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

include_directories(
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${STB_DIR}
    ${TINYGLTF_DIR}
    ${IMGUIZMO_DIR}
    ${FONTS_DIR}
    ${ICONFONTCPPHEADERS_DIR}
)

# ImGuizmo expects ImVec2 +/-/* operators, which ImGui only declares when
# this is defined - must be a compiler-level define (not a local #define
# inside ImGuizmo.cpp) so it's already active the first time imgui.h is
# included in that translation unit, before imgui.h's own #pragma once
# guard locks in whether those operators got declared.
add_compile_definitions(IMGUI_DEFINE_MATH_OPERATORS)

add_subdirectory(third_party/nfd)

# Everything below src/ui (the file formats, mesh-editing ops, GL rendering,
# and OBJ/glTF import-export) has no ImGui dependency - see
# docs/ARCHITECTURE.md. Built once as an OBJECT library so both the app and
# the tests/ suite link the same compiled objects instead of recompiling
# them twice.
set(TIMEDITOR_CORE_SOURCES
    src/core/tim_parser.cpp
    src/core/tim_document.cpp
    src/core/tim_writer.cpp
    src/core/vram_manager.cpp
    src/core/tmd_parser.cpp
    src/core/tmd_writer.cpp
    src/core/tmd_mesh_ops.cpp
    src/core/tmd_document.cpp
    src/gfx/tim_texture_builder.cpp
    src/gfx/image_exporter.cpp
    src/gfx/image_quantizer.cpp
    src/gfx/raster_ops.cpp
    src/gfx/gl_ext.cpp
    src/gfx/framebuffer.cpp
    src/gfx/tmd_texture_cache.cpp
    src/gfx/tmd_object_renderer.cpp
    src/gfx/texture_atlas.cpp
    src/gfx/tmd_mesh_import.cpp
    src/gfx/tmd_export_material.cpp
    src/gfx/tmd_obj_export.cpp
    src/gfx/tmd_obj_import.cpp
    src/gfx/tmd_gltf_export.cpp
    src/gfx/tmd_gltf_import.cpp
)
add_library(TIMEditorCore OBJECT ${TIMEDITOR_CORE_SOURCES})
# gl_ext.cpp loads OpenGL extension function pointers via
# glfwGetProcAddress - GLFW is a link dependency even though nothing in
# this layer opens a window (the main app already creates the one used).
target_link_libraries(TIMEditorCore PUBLIC OpenGL::GL glfw)

add_executable(TIMEditor
    src/main.cpp
    src/ui/theme.cpp
    src/ui/inspector_panel.cpp
    src/ui/vram_panel.cpp
    src/ui/vram_panel_draw.cpp
    src/ui/editor_app.cpp
    src/ui/palette_view.cpp
    src/ui/export_dialog.cpp
    src/ui/import_image_dialog.cpp
    src/ui/image_editor_panel.cpp
    src/ui/file_dialog.cpp
    src/ui/tmd_panel.cpp
    src/ui/tmd_raw_editor.cpp
    src/ui/tmd_raw_editor_fields.cpp
    src/ui/model_editor_panel.cpp
    src/ui/model_editor_input.cpp
    src/ui/model_editor_uv.cpp
    src/ui/icon_font.cpp
    ${IMGUIZMO_DIR}/ImGuizmo.cpp
    src/ui/model_export_dialog.cpp
    src/ui/model_import_dialog.cpp
    ${IMGUI_SOURCES}
)

target_link_libraries(TIMEditor PRIVATE TIMEditorCore glfw OpenGL::GL nfd)

option(TIMEDITOR_BUILD_TESTS "Build the tests/ suite (ctest)" ON)
if(TIMEDITOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
