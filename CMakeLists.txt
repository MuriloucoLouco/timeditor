cmake_minimum_required(VERSION 3.15)
project(TIMEditor)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

set(IMGUI_DIR third_party/imgui)
set(STB_DIR third_party/stb)

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
)

add_subdirectory(third_party/nfd)

add_executable(TIMEditor
    src/main.cpp
    src/core/tim_parser.cpp
    src/core/tim_document.cpp
    src/core/tim_writer.cpp
    src/core/vram_manager.cpp
    src/gfx/tim_texture_builder.cpp
    src/gfx/image_exporter.cpp
    src/gfx/image_quantizer.cpp
    src/ui/theme.cpp
    src/ui/inspector_panel.cpp
    src/ui/vram_panel.cpp
    src/ui/editor_app.cpp
    src/ui/palette_view.cpp
    src/ui/export_dialog.cpp
    src/ui/import_image_dialog.cpp
    src/ui/image_editor_panel.cpp
    src/ui/file_dialog.cpp
    ${IMGUI_SOURCES}
)

target_link_libraries(TIMEditor PRIVATE glfw OpenGL::GL nfd)