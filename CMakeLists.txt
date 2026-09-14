cmake_minimum_required(VERSION 3.15)
project(TIMEditor)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

set(IMGUI_DIR third_party/imgui)
set(STB_DIR third_party/stb)
set(TINYGLTF_DIR third_party/tinygltf)
set(IMGUIZMO_DIR third_party/ImGuizmo/src) # tag 1.10+ moved ImGuizmo.h/.cpp under src/
set(FONTS_DIR third_party/fonts)
set(ICONFONTCPPHEADERS_DIR third_party/IconFontCppHeaders)

file(GLOB IMGUI_SOURCES
    ${IMGUI_DIR}/*.cpp
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

add_executable(TIMEditor
    src/main.cpp
    src/core/tim_parser.cpp
    src/core/tim_document.cpp
    src/core/tim_writer.cpp
    src/core/vram_manager.cpp
    src/core/tmd_parser.cpp
    src/core/tmd_writer.cpp
    src/core/tmd_mesh_ops.cpp
    src/gfx/tim_texture_builder.cpp
    src/gfx/image_exporter.cpp
    src/gfx/image_quantizer.cpp
    src/gfx/gl_ext.cpp
    src/gfx/framebuffer.cpp
    src/gfx/tmd_texture_cache.cpp
    src/gfx/tmd_object_renderer.cpp
    src/gfx/texture_atlas.cpp
    src/gfx/tmd_mesh_import.cpp
    src/gfx/tmd_obj_export.cpp
    src/gfx/tmd_obj_import.cpp
    src/gfx/tmd_gltf_export.cpp
    src/gfx/tmd_gltf_import.cpp
    src/ui/theme.cpp
    src/ui/inspector_panel.cpp
    src/ui/vram_panel.cpp
    src/ui/editor_app.cpp
    src/ui/palette_view.cpp
    src/ui/export_dialog.cpp
    src/ui/import_image_dialog.cpp
    src/ui/image_editor_panel.cpp
    src/ui/file_dialog.cpp
    src/ui/tmd_panel.cpp
    src/ui/tmd_raw_editor.cpp
    src/ui/model_editor_panel.cpp
    src/ui/icon_font.cpp
    ${IMGUIZMO_DIR}/ImGuizmo.cpp
    src/ui/model_export_dialog.cpp
    src/ui/model_import_dialog.cpp
    ${IMGUI_SOURCES}
)

target_link_libraries(TIMEditor PRIVATE glfw OpenGL::GL nfd)