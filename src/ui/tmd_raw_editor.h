#pragma once
#include "../core/tim_document.h"
#include "../core/tmd_format.h"
#include "../core/vram_manager.h"
#include "../gfx/framebuffer.h"
#include "../gfx/tmd_texture_cache.h"
#include <functional>
#include <string>
#include <vector>

namespace ui {

// The TMD Editor's "Raw Editor" sub-tab: a tree of one object's Vertices/
// Normals/Primitives tables, and a field editor on the right for whatever
// is checked in it - mirroring how the TIM Editor exposes structured
// editing, but for TMD's raw structural data instead of pixels.
//
// Selection is checkbox-driven, like the TIM Editor's own per-image
// checkboxes: checking a box (or a "select all" per section) adds to a
// multi-select, all of one kind at a time (checking a normal clears any
// checked vertices/primitives); clicking a row's label instead replaces
// the selection with just that one item. Exactly one checked item shows a
// normal single-item editor ("Edit Vertex #3"); two or more of the same
// kind switch the pane to a batch editor for that kind.
//
// Doesn't know about TmdPanel's LoadedModel/undo-stack bookkeeping: the
// caller passes the object to edit directly plus two small callbacks
// (`push_undo`, called once before a field is about to change; `mark_dirty`,
// called after it does) so this class only ever deals with one
// tmd::TMD_Object at a time.
class TmdRawEditor {
public:
    void Render(int model_index, int object_index, tmd::TMD_Object& obj, const tim::Document& document,
                VRAMManager& vram_manager, gfx::TmdTextureCache& texture_cache,
                const std::function<void()>& push_undo, const std::function<void()>& mark_dirty);

    // Programmatically selects one primitive as a normal single selection -
    // a "jump here" entry point for other panels (e.g. the Model Editor's
    // per-selection field panel linking back to this primitive's full,
    // byte-level field editor).
    void SelectPrimitive(int model_index, int object_index, int primitive_index);

private:
    enum class FieldKind { None, Vertex, Normal, Primitive };

    int last_model_index = -1;
    int last_object_index = -1;

    FieldKind checked_kind = FieldKind::None;
    std::vector<bool> vertex_checked;
    std::vector<bool> normal_checked;
    std::vector<bool> primitive_checked;

    float tree_width = 260.0f;
    float uv_zoom = 4.0f;
    bool uv_panning_active = false; // right-drag-to-pan session - see zoom_pan.h's PanWithMouseDrag
    float fields_width = 380.0f;
    bool show_tpage_overlay = false;

    // Batch-edit forms' pending values, kept across frames while the user
    // fills them in.
    float batch_vertex_offset[3] = { 0, 0, 0 };
    int batch_normal_dir[3] = { 0, 0, 4096 };
    struct BatchPrimitiveState {
        bool set_textured = false, textured_value = false;
        bool set_gouraud = false, gouraud_value = false;
        bool set_no_light = false, no_light_value = false;
        bool set_double_sided = false, double_sided_value = false;
        bool set_semi_transparent = false, semi_transparent_value = false;
        bool set_tpage_x = false; int tpage_x_value = 0;
        bool set_tpage_y = false; int tpage_y_value = 0;
        bool set_color_mode = false; int color_mode_value = 0;
        bool set_semi_rate = false; int semi_rate_value = 0;
        bool set_clut_x = false; int clut_x_value = 0;
        bool set_clut_y = false; int clut_y_value = 0;
        bool guess_clut = false; // per-primitive, overrides set_clut_x/y when checked
        bool set_color = false; float color_value[3] = { 0.5f, 0.5f, 0.5f };
    } batch_primitive;

    gfx::Framebuffer preview_framebuffer;

    void ClearAllChecks();
    std::vector<bool>& CheckedArrayFor(FieldKind kind);
    std::vector<int> CheckedIndices(FieldKind kind);

    void RenderTree(tmd::TMD_Object& obj);
    void RenderTreeSection(const char* title, size_t count, FieldKind kind, std::vector<bool>& checked,
                            const std::function<std::string(int)>& label_fn);
    void RenderFieldEditor(tmd::TMD_Object& obj, const tim::Document& document, VRAMManager& vram_manager,
                            gfx::TmdTextureCache& texture_cache, const std::function<void()>& push_undo,
                            const std::function<void()>& mark_dirty);

    void RenderVertexEditor(tmd::TMD_Object& obj, int index, const std::function<void()>& push_undo,
                             const std::function<void()>& mark_dirty);
    void RenderNormalEditor(tmd::TMD_Object& obj, int index, const std::function<void()>& push_undo,
                             const std::function<void()>& mark_dirty);
    void RenderPrimitiveEditor(tmd::TMD_Object& obj, int index, const tim::Document& document,
                                VRAMManager& vram_manager, gfx::TmdTextureCache& texture_cache,
                                const std::function<void()>& push_undo, const std::function<void()>& mark_dirty);
    void RenderPrimitiveFields(tmd::TMD_Object& obj, int index, const tim::Document& document,
                                const std::function<void()>& push_undo, const std::function<void()>& mark_dirty);
    void RenderUvPreview(tmd::TMD_Object& obj, int index, VRAMManager& vram_manager,
                          gfx::TmdTextureCache& texture_cache, const std::function<void()>& push_undo,
                          const std::function<void()>& mark_dirty);
    void RenderPrimitive3DPreview(tmd::TMD_Object& obj, int index, VRAMManager& vram_manager,
                                   gfx::TmdTextureCache& texture_cache);

    void RenderBatchVertexEditor(tmd::TMD_Object& obj, const std::vector<int>& indices,
                                  const std::function<void()>& push_undo, const std::function<void()>& mark_dirty);
    void RenderBatchNormalEditor(tmd::TMD_Object& obj, const std::vector<int>& indices,
                                  const std::function<void()>& push_undo, const std::function<void()>& mark_dirty);
    void RenderBatchPrimitiveEditor(tmd::TMD_Object& obj, const std::vector<int>& indices,
                                     const tim::Document& document, const std::function<void()>& push_undo,
                                     const std::function<void()>& mark_dirty);
};

} // namespace ui
