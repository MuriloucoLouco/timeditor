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

First, get the source with its submodules (every third-party dependency
under `third_party/` is vendored this way - see `docs/ARCHITECTURE.md` for
the full list and why each one is there):

```sh
git clone --recurse-submodules https://github.com/MuriloucoLouco/timeditor
cd timeditor
# already cloned without submodules? run instead:
git submodule update --init --recursive
```

Then follow whichever OS section below applies.

### Linux

1. Install a compiler, CMake, and GLFW3 (Debian/Ubuntu):
   ```sh
   sudo apt install build-essential cmake libglfw3-dev
   ```
2. Configure and build:
   ```sh
   mkdir build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   cmake --build . -j$(nproc)
   ```

This produces `build/TIMEditor`.

### Windows

1. Install a C++ compiler and CMake, for example via Visual Studio.
2. Install [vcpkg](https://github.com/microsoft/vcpkg) to install GLFW3:
   ```powershell
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   C:\vcpkg\vcpkg.exe install glfw3:x64-windows
   ```
3. Configure and build:
   ```powershell
   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
   cmake --build build --config Release -j
   ```
4. Copy the one runtime dependency GLFW needs alongside the exe:
   ```powershell
   copy C:\vcpkg\installed\x64-windows\bin\glfw3.dll build\Release\
   ```

This produces `build/Release/TIMEditor.exe`.

### Tests

```sh
cd build
ctest --output-on-failure
```

Disable with `-DTIMEDITOR_BUILD_TESTS=OFF`.

### Running in a VM

VirtualBox's SVGA3D on Windows may fail with OpenGL. Swap in Mesa's own
software renderer instead of the VM's passthrough driver. You may download
the x64 release build from https://github.com/pal1000/mesa-dist-win/releases.
From the archive, copy `opengl32.dll` and `libgallium_wgl.dll` into the
same folder as `TIMEditor.exe`.

## Architecture

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for layering, document
design, coordinate conventions, undo/redo, and the vendored-dependency
table.
