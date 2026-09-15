// TmdRawEditor's tree/dispatch: the Vertices/Normals/Primitives checkbox
// tree and the routing logic that picks which field editor to show for the
// current selection. The actual field editors (single-item and batch, for
// all three kinds) live in tmd_raw_editor_fields.cpp - split purely to keep
// this file from growing unmanageably long, see docs/ARCHITECTURE.md.
#include "tmd_raw_editor.h"
#include "splitter.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>

namespace ui {

namespace {

std::string SummarizePolygon(const tmd::TMD_Polygon& p) {
    std::string s = (p.num_verts == 4) ? "Quad" : "Tri";
    s += p.textured ? ", Textured" : ", Flat";
    if (p.gouraud) s += ", Gouraud";
    if (p.no_light) s += ", NoLight";
    if (p.semi_transparent) s += ", SemiTrans";
    if (p.double_sided) s += ", 2Sided";
    return s;
}

} // namespace

void TmdRawEditor::Render(int model_index, int object_index, tmd::TMD_Object& obj, const tim::Document& document,
                           VRAMManager& vram_manager, gfx::TmdTextureCache& texture_cache,
                           const std::function<void()>& push_undo, const std::function<void()>& mark_dirty) {
    // Also re-check by content size, not just index: the object's contents
    // can change out from under this panel without going through any of
    // its own edits (Undo/Redo, an OBJ/glTF reimport), which is the only
    // path that otherwise keeps vertex_checked/normal_checked/
    // primitive_checked sized to match - see the identical fix/comment in
    // ModelEditorPanel::Render.
    if (model_index != last_model_index || object_index != last_object_index ||
        obj.vertices.size() != vertex_checked.size() || obj.normals.size() != normal_checked.size() ||
        obj.polygons.size() != primitive_checked.size()) {
        checked_kind = FieldKind::None;
        vertex_checked.assign(obj.vertices.size(), false);
        normal_checked.assign(obj.normals.size(), false);
        primitive_checked.assign(obj.polygons.size(), false);
        last_model_index = model_index;
        last_object_index = object_index;
    }

    ImGui::BeginChild("TmdRawTree", ImVec2(tree_width, 0), true);
    ImGui::TextWrapped("Check items to batch-edit them together.");
    ImGui::Separator();
    RenderTree(obj);
    ImGui::EndChild();

    tree_width += ui::VerticalSplitter("TmdRawSplitter");
    tree_width = std::clamp(tree_width, 180.0f, 480.0f);

    ImGui::BeginChild("TmdRawFields", ImVec2(0, 0), true);
    RenderFieldEditor(obj, document, vram_manager, texture_cache, push_undo, mark_dirty);
    ImGui::EndChild();
}

void TmdRawEditor::SelectPrimitive(int model_index, int object_index, int primitive_index) {
    last_model_index = model_index;
    last_object_index = object_index;
    ClearAllChecks();
    checked_kind = FieldKind::Primitive;
    if (primitive_index >= 0 && primitive_index < static_cast<int>(primitive_checked.size())) {
        primitive_checked[primitive_index] = true;
    }
}

void TmdRawEditor::ClearAllChecks() {
    std::fill(vertex_checked.begin(), vertex_checked.end(), false);
    std::fill(normal_checked.begin(), normal_checked.end(), false);
    std::fill(primitive_checked.begin(), primitive_checked.end(), false);
}

std::vector<bool>& TmdRawEditor::CheckedArrayFor(FieldKind kind) {
    if (kind == FieldKind::Vertex) return vertex_checked;
    if (kind == FieldKind::Normal) return normal_checked;
    return primitive_checked;
}

std::vector<int> TmdRawEditor::CheckedIndices(FieldKind kind) {
    std::vector<int> result;
    if (kind == FieldKind::None) return result;
    const auto& arr = CheckedArrayFor(kind);
    for (size_t i = 0; i < arr.size(); i++) {
        if (arr[i]) result.push_back(static_cast<int>(i));
    }
    return result;
}

void TmdRawEditor::RenderTree(tmd::TMD_Object& obj) {
    RenderTreeSection("Vertices", obj.vertices.size(), FieldKind::Vertex, vertex_checked, [&](int i) {
        const auto& v = obj.vertices[i];
        char label[64];
        snprintf(label, sizeof(label), "Vertex #%d: (%d, %d, %d)", i, v.x, v.y, v.z);
        return std::string(label);
    });

    RenderTreeSection("Normals", obj.normals.size(), FieldKind::Normal, normal_checked, [&](int i) {
        const auto& n = obj.normals[i];
        char label[64];
        snprintf(label, sizeof(label), "Normal #%d: (%d, %d, %d)", i, n.x, n.y, n.z);
        return std::string(label);
    });

    RenderTreeSection("Primitives", obj.polygons.size(), FieldKind::Primitive, primitive_checked,
                       [&](int i) { return "Primitive #" + std::to_string(i) + ": " + SummarizePolygon(obj.polygons[i]); });
}

// One "Vertices"/"Normals"/"Primitives" section: a "select all" checkbox
// next to the section header, then one row per item with its own
// checkbox (additive multi-select - ticking one never touches the
// others) plus a clickable label (replaces the whole selection with just
// this one item) - the same checkbox-vs-label duality the TIM Editor's
// file list already uses for its own per-image selection.
void TmdRawEditor::RenderTreeSection(const char* title, size_t count, FieldKind kind, std::vector<bool>& checked,
                                      const std::function<std::string(int)>& label_fn) {
    ImGui::PushID(title);

    bool all_checked = count > 0 && checked_kind == kind;
    if (all_checked) {
        for (size_t i = 0; i < count; i++) {
            if (!checked[i]) { all_checked = false; break; }
        }
    }
    if (ImGui::Checkbox("##select_all", &all_checked)) {
        ClearAllChecks();
        if (all_checked) {
            checked_kind = kind;
            checked.assign(count, true);
        } else {
            checked_kind = FieldKind::None;
        }
    }
    ImGui::SameLine();

    bool open = ImGui::TreeNodeEx("##section", ImGuiTreeNodeFlags_DefaultOpen, "%s (%zu)", title, count);
    if (open) {
        for (int i = 0; i < static_cast<int>(count); i++) {
            ImGui::PushID(i);
            bool item_checked = (checked_kind == kind) && checked[i];
            if (ImGui::Checkbox("##chk", &item_checked)) {
                if (checked_kind != kind) {
                    ClearAllChecks();
                    checked_kind = kind;
                }
                checked[i] = item_checked;
                if (std::none_of(checked.begin(), checked.end(), [](bool b) { return b; })) checked_kind = FieldKind::None;
            }
            ImGui::SameLine();

            bool is_sole_selection = (checked_kind == kind) && checked[i];
            if (ImGui::Selectable(label_fn(i).c_str(), is_sole_selection)) {
                ClearAllChecks();
                checked_kind = kind;
                checked[i] = true;
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void TmdRawEditor::RenderFieldEditor(tmd::TMD_Object& obj, const tim::Document& document, VRAMManager& vram_manager,
                                      gfx::TmdTextureCache& texture_cache, const std::function<void()>& push_undo,
                                      const std::function<void()>& mark_dirty) {
    if (checked_kind == FieldKind::None) {
        ImGui::TextDisabled("Select a vertex, normal, or primitive in the tree to inspect/edit it.");
        return;
    }

    std::vector<int> indices = CheckedIndices(checked_kind);
    if (indices.empty()) {
        ImGui::TextDisabled("Select a vertex, normal, or primitive in the tree to inspect/edit it.");
        return;
    }

    if (indices.size() == 1) {
        switch (checked_kind) {
            case FieldKind::Vertex: RenderVertexEditor(obj, indices[0], push_undo, mark_dirty); break;
            case FieldKind::Normal: RenderNormalEditor(obj, indices[0], push_undo, mark_dirty); break;
            case FieldKind::Primitive:
                RenderPrimitiveEditor(obj, indices[0], document, vram_manager, texture_cache, push_undo, mark_dirty);
                break;
            case FieldKind::None: break;
        }
        return;
    }

    const char* kind_name = checked_kind == FieldKind::Vertex ? "vertices" : checked_kind == FieldKind::Normal ? "normals" : "primitives";
    ImGui::Text("Batch Edit (%zu %s)", indices.size(), kind_name);
    ImGui::Separator();
    switch (checked_kind) {
        case FieldKind::Vertex: RenderBatchVertexEditor(obj, indices, push_undo, mark_dirty); break;
        case FieldKind::Normal: RenderBatchNormalEditor(obj, indices, push_undo, mark_dirty); break;
        case FieldKind::Primitive: RenderBatchPrimitiveEditor(obj, indices, document, push_undo, mark_dirty); break;
        case FieldKind::None: break;
    }
}

} // namespace ui
