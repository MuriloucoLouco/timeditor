# TIMEditor

A desktop editor for PlayStation 1 **TIM** (image) and **TMD** (3D model)
files, built with Dear ImGui + GLFW + OpenGL.

## Features

- **TIM Editor** - view/edit pixels, palettes, and CLUTs; switch BPP;
  import/export; undo/redo.
- **VRAM Viewer** - scrollable/zoomable view of the emulated VRAM buffer,
  with every TIM's image and CLUT drawn as a draggable, snap-to-grid
  overlay; flags overlaps and framebuffer collisions.
- **TMD Editor** - shares the same `VRAMManager` as the TIM Editor, so
  textures resolve identically:
  - **3D View** - textured/lit preview with per-object placement and an
    orbit + WASD camera.
  - **Model Editor** - interactive mesh editing (vertex/edge/face selection,
    gizmo transforms, extrude/merge/duplicate, normals) plus a UV workspace.
  - **Raw Editor** - field-level tree view of a model's
    vertices/normals/primitives with a live 3D preview.
  - OBJ and glTF import/export (see `docs/ARCHITECTURE.md` for round-trip
    limits).
- Independent undo/redo for the TIM and TMD sides.

## Building

Requires CMake 3.15+, a C++17 compiler, OpenGL, and GLFW3.

```sh
git clone --recurse-submodules <this repo's URL>
# already cloned without submodules? run:
git submodule update --init --recursive
```

### Linux

```sh
sudo apt install libglfw3-dev
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

Produces `build/TIMEditor`.

### Windows

GLFW3 isn't vendored, so grab it via
[vcpkg](https://github.com/microsoft/vcpkg), from a Developer Command Prompt /
PowerShell:

```powershell
vcpkg install glfw3:x64-windows

cmake -B build -S . `
  -DCMAKE_TOOLCHAIN_FILE="<vcpkg-root>/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release -j

copy "<vcpkg-root>\installed\x64-windows\bin\glfw3.dll" build\Release\
```

Produces `build/Release/TIMEditor.exe` (OpenGL itself comes from Windows).

### Tests

```sh
cd build
ctest --output-on-failure
```

Disable with `-DTIMEDITOR_BUILD_TESTS=OFF`.

## Architecture

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for layering, document
design, coordinate conventions, undo/redo, and the vendored-dependency
table.
