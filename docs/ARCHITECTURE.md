# Architecture

This document describes the shape of the codebase for anyone working on it:
the layering rule between `src/core`, `src/gfx`, and `src/ui`; how the TIM
and TMD sides mirror each other; the coordinate conventions a PS1 file's raw
data goes through on its way to the screen; the undo/redo design; and a
table of every vendored third-party dependency and why it's there.

## The `core` / `gfx` / `ui` layering

The source tree is a flat, three-layer stack (not further subdivided into
subfolders - the layer itself, plus each file's name, already does the
grouping job at the project's current size):

- **`src/core`** - the file formats themselves, and the data/logic that owns
  them: TIM/TMD structs, parsers, writers, `tmd_mesh_ops`'s mesh-editing
  primitives (extrude, merge, duplicate, delete...), `tim::Document`, and
  `tmd::TmdDocument`. No ImGui dependency anywhere in this layer, and (with
  one deliberate, named exception below) no OpenGL either - it's pure data
  and file I/O, testable with no window or GL context at all.
- **`src/gfx`** - OpenGL-dependent rendering and format-bridging that `core`
  data needs but shouldn't own itself: texture building/caching, the TMD
  object renderer, OBJ/glTF import-export, texture-atlas packing, the raw
  pixel-buffer operations behind the Image Editor's paint tools
  (`raster_ops.h`). Depends on `core`, never on `ui`. No ImGui dependency
  here either - it's real OpenGL calls and pixel math, but nothing about
  presenting a UI.
- **`src/ui`** - every ImGui panel, dialog, and widget helper: the actual
  application. Depends on both layers below it.

The dependency rule is enforced by convention, not a build-system check, but
it holds throughout the codebase as of this writing (`core` never includes a
`gfx` or `ui` header, `gfx` never includes a `ui` header, and neither layer
includes `imgui.h`).

**The one exception**: `core/vram_manager.cpp` directly owns a live OpenGL
texture (`#include <GL/gl.h>`, `VRAMManager::GetVRAMTextureID()`) instead of
being pure data like the rest of `core`. This is a deliberate, long-standing
choice, not an oversight - the emulated VRAM display is central to the whole
app and tightly coupled to the VRAM buffer itself, and every other type
`VRAMManager` deals with (`TIM_Image`, `TMD_Polygon`'s texpage/CLUT fields)
already lives in `core`.

## The TIM / TMD domain split

The editor has two independent "document" domains, TIM images and TMD
models, each following the same Document/Panel separation:

- **TIM side**: `tim::Document` (`core/tim_document.h`) owns every loaded
  `TIM_Image`, VRAM-origin moves, content edits (paint strokes, BPP
  switches, palette edits), and their undo/redo history. `InspectorPanel`
  (and the VRAM Viewer's `VRAMPanel`) are thin UI layers over it - they read
  `document.Images()` to render, and call into `Document`'s methods for
  every actual edit.
- **TMD side**: `tmd::TmdDocument` (`core/tmd_document.h`) owns every loaded
  `.tmd` file (as `LoadedModel { TMD_Model, per-object transforms/
  visibility, dirty }`), which model/object is active, and the undo/redo
  history for edits to an object's fields. `TmdPanel` is the thin UI layer
  over it, holding a `TmdDocument` member and forwarding to it - `TmdPanel`
  itself keeps only rendering/UI-only state: the 3D-View camera, render
  toggles (textured/wireframe/cull-backfaces), the GL framebuffer and
  texture cache, and the sidebar/dialogs/tab UI. The Model Editor and Raw
  Editor sub-tabs it hosts don't touch `TmdDocument` directly either - they
  take the one `tmd::TMD_Object&` being edited plus `push_undo`/`mark_dirty`
  callbacks, the same calling convention either way.

Both documents stay free of ImGui and (`VRAMManager`'s exception aside)
OpenGL, so both are directly unit-testable - see `tests/test_tmd_document.cpp`
for `TmdDocument`'s coverage.

## Coordinate conventions

- **Y-down storage, Y-up viewer space.** A `.tmd` file stores vertex and
  normal coordinates PS1-native: Y-down (matching the hardware's GTE and
  screen-space convention). Every renderer and every export path negates Y
  once, via `gfx::ToViewerSpace`/`gfx::ToExportSpace` (`gfx/tmd_space.h`),
  so a conventional Y-up camera or DCC tool shows the model upright; every
  reimport path negates it back on the way in (the negation is its own
  inverse, so the two helpers are the same formula, just named for which
  direction data is moving).
- **Vertex positions** are raw PS-X fixed-point units with no fixed
  real-world scale - a `.tmd` file doesn't encode a unit system, so what
  "one unit" means is whatever the original modeling tool used.
- **Normals** use the GTE's fixed-point convention: `4096` along one axis
  represents `1.0` (a unit vector's component), independent of the vertex
  scale above.
- **UV coordinates** are a single byte (0-255) per corner, addressing a
  primitive's own texpage-relative tile - see `gfx/tmd_export_material.h`'s
  `TileWidthForTsb` for how a texpage's BPP mode (4bpp/8bpp/16bpp) changes
  how wide that tile actually is, which is also the source of the
  documented OBJ/glTF reimport asymmetry below.

### OBJ/glTF round-trip: documented, intentional limits

Exporting a model to OBJ/glTF and reimporting it (without edits) does **not**
reproduce every original field exactly - this is documented behavior, not a
bug (see `tests/test_obj_roundtrip.cpp`/`test_gltf_roundtrip.cpp`, which
assert the actual contract rather than full identity):

- Every reimported polygon has its shading-mode flags reset to defaults
  (`gouraud`, `no_light`, `double_sided`, `semi_transparent`,
  `color_per_vertex` all become `false`, `tsb`/`cba` become `0`) - a plain
  OBJ/glTF has no PS1-specific concept of any of these, so reimport can't
  recover them; fix them up afterward with the Raw Editor.
- The OBJ exporter unconditionally writes a UV coordinate for every face
  (textured or not), so every reimported face ends up `textured = true`
  regardless of whether it originally was.
- Reimport always assumes a 256-wide/4bpp tile when converting a normalized
  UV back to a byte 0-255 value. A textured primitive whose *original* tile
  was narrower (8bpp/128-wide, or 16bpp/64-wide) will not round-trip its UV
  values exactly even once the mode-reset above is accounted for - the
  export side divides by the primitive's *actual* tile width, but reimport
  can't know what that was after the mode reset.

## Undo/redo design

- **`tim::Document`** has the deeper design of the two, since painting
  naturally wants many small undo steps: a move/delete stack
  (`PushUndoSnapshot`/`DeleteImage`) and a separate, denser content-edit
  stack (`PushContentUndoSnapshot`, for paint strokes/BPP switches/canvas
  resizes/imports/palette edits), each entry tagged with a monotonically
  increasing sequence number. `Undo()` compares the two stacks' top `seq`
  values to undo whichever kind happened most recently, moving what it
  overwrites onto a single LIFO `redo_stack` so `Redo()` never has to repeat
  that comparison - undoing always fully determines, in the moment, exactly
  one entry to redo next.
- **`tmd::TmdDocument`** is simpler: one `ObjectSnapshot` stack (whole-object
  copies, keyed by model/object index) for `undo_stack`/`redo_stack`, since
  there's no VRAM/texture coupling to invalidate on the TMD side, just
  object data. `PushUndo(model_index, object_index)` is called once before
  any operation that mutates an object's fields (a Raw Editor field edit, a
  Model Editor mesh operation, an OBJ import apply); any new `PushUndo`
  clears `redo_stack`, same as `tim::Document`.

Both panels also guard against a subtler bug class: since the document can
change an object's vertex/normal/polygon *count* out from under a UI panel
without going through that panel's own editing calls (an `Undo()`/`Redo()`
swaps a whole `TMD_Object` back in wholesale; so does an OBJ/glTF reimport
apply), `ModelEditorPanel`/`TmdRawEditor` re-check the live object's counts
against their own selection-state vectors every frame (not just whether the
model/object *index* changed) before trusting those vectors' sizes - a stale,
wrong-sized `std::vector<bool>` selection silently indexes out of bounds
rather than throwing, so this has to be checked proactively rather than
relying on it to fail loudly.

## Vendored dependencies

Every third-party dependency lives under `third_party/` as a git submodule
(see `.gitmodules`) except the two that are just a handful of files with no
upstream build of their own (`fonts`, `IconFontCppHeaders`), which are
committed directly instead.

| Dependency | Version/tag | License | Why it's here |
|---|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | pinned commit past tag `v1.62` (upstream's main branch) | MIT | The immediate-mode GUI framework the whole app is built on. |
| [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) | tag `1.10` | MIT | The Model Editor's on-screen move/rotate/scale gizmo. Pinned to `1.10` specifically (a *later* release than the lower-looking `1.83`, despite the version number - see `git tag --sort=-creatordate` if that's ever unclear again) because it's the first tag to include upstream's own fixes for two API-skew issues against this project's ImGui version (`CaptureMouseFromApp`/`SetNextFrameWantCaptureMouse` input-capture, and `AddPolyline`'s argument order) that otherwise had to be patched by hand locally - don't "helpfully" revert to `1.83`. |
| [stb](https://github.com/nothings/stb) (`stb_image`, `stb_image_write`) | pinned commit on upstream's main branch | MIT / Public Domain (dual) | Image loading/encoding for import/export paths that need a conventional image format. |
| [tinyfiledialogs](https://tinyfiledialogs.sourceforge.net) | pinned commit on upstream's `master` branch | zlib | Native OS open/save file dialogs, wrapped by `src/ui/file_dialog.cpp`. Native Win32 dialogs on Windows; on Linux it shells out to whatever's installed (`zenity`, `kdialog`, etc.) at runtime instead of linking a GUI toolkit, so - unlike the GTK3-based `nfd` it replaced - it needs no extra dev packages to build. |
| [tinygltf](https://github.com/syoyo/tinygltf) | pinned commit past tag `v2.8.1` (upstream's main branch) | MIT | glTF 2.0 import/export for the TMD Editor's model exchange path. |
| Fonts (`third_party/fonts`) | committed directly, not a submodule | Font Awesome Free (icons: CC BY 4.0, font: SIL OFL 1.1) + Roboto (Apache License 2.0) | The embedded icon font (toolbar/button icons throughout the UI) and the UI's own text font, both baked into compressed C header arrays so the app has no external font files to ship. |
| [IconFontCppHeaders](https://github.com/juliettef/IconFontCppHeaders) | committed directly, not a submodule | zlib | C++ header mapping Font Awesome's icon names to the codepoint constants (`ICON_FA_*`) the UI code references. |
