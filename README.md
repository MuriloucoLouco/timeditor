# TIMEditor

A desktop editor for PlayStation 1 **TIM** (image) and **TMD** (3D model)
files, built with Dear ImGui + GLFW + OpenGL as an immediate-mode desktop
app. It emulates enough of the PS1's VRAM addressing model that a model's
textures are shown exactly the way the real hardware would resolve them -
place a TIM image and its CLUT somewhere in VRAM, and any TMD polygon whose
texpage/CLUT fields point there picks it up automatically, same as on
console.

## Features

- **TIM Editor** - load, inspect, and edit `.tim` image files: view/switch
  CLUTs, edit pixels and palette colors directly, add/remove palette rows,
  switch BPP mode, import/export images, undo/redo.
- **VRAM Viewer** - a scrollable/zoomable view of the emulated 1024x512-word
  VRAM buffer, with every loaded TIM's image and CLUT block drawn as an
  overlay. Click to select, drag to move (snaps to the TPage grid and to
  other TIMs), arrow keys to nudge, overlapping regions are flagged, and a
  display-buffer preset overlay shows common PS1 framebuffer footprints so
  placing a texture over the frame buffer is a visible mistake instead of a
  silent VRAM collision.
- **TMD Editor** - load and edit `.tmd` model files, addressing VRAM through
  the same shared `VRAMManager` as the TIM Editor:
  - **3D View** - a read-only, textured/lit preview of every loaded model,
    with a per-object placement transform (position/rotation/scale - not
    stored in the file, just a viewer convenience) and an orbit + WASD fly
    camera.
  - **Model Editor** - a real interactive mesh editor: select vertices,
    edges, or faces; move/rotate/scale the selection with an on-screen
    gizmo; extrude, merge, duplicate, flip/recalculate normals, delete; a
    tile-scoped UV workspace for texture mapping.
  - **Raw Editor** - a field-level, checkbox-driven tree of one object's
    vertices/normals/primitives, with single-item and batch editors, a
    per-primitive UV preview, and a highlighted 3D preview of whichever
    primitive is selected.
  - OBJ and glTF import/export for taking geometry out to (and back from) a
    conventional 3D modeling tool - see `docs/ARCHITECTURE.md` for the
    documented, intentional limits of that round-trip.
- Undo/redo, independently on the TIM side and the TMD side.

## Building

Requirements: CMake 3.15+, a C++17 compiler, OpenGL, and GLFW3. On Linux,
the native file dialog library (`nfd`) additionally needs GTK3 and Wayland
client development packages - on Debian/Ubuntu:

```sh
sudo apt install libglfw3-dev libgtk-3-dev libwayland-dev pkg-config
```

Clone with submodules (every third-party dependency under `third_party/` is
vendored as a git submodule - see `docs/ARCHITECTURE.md` for the full list
and why each one is there):

```sh
git clone --recurse-submodules <this repo's URL>
# or, if you already cloned without it:
git submodule update --init --recursive
```

Then configure and build:

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

This produces the `TIMEditor` executable in `build/`.

### Running the tests

The `tests/` suite (registered with CMake's `ctest`) covers the ImGui-free
`core`/`gfx` layers directly - ray/picking math, mesh-editing operations,
OBJ/glTF import-export round-tripping, the TMD document's undo/redo, etc.
It's on by default; disable it with `-DTIMEDITOR_BUILD_TESTS=OFF` if you
don't want it built.

```sh
cd build
ctest --output-on-failure
```

## Architecture

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the `core`/`gfx`/`ui`
layering rule, the TIM/TMD document design, coordinate conventions, the
undo/redo design, and the full vendored-dependency table.
