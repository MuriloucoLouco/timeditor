#include "tmd_panel.h"
#include "../core/tmd_writer.h"
#include "../gfx/tmd_obj_import.h"
#include "../gfx/tmd_object_renderer.h"
#include "../gfx/tmd_space.h"
#include "file_dialog.h"
#include "splitter.h"
#include "text_utils.h"
#include "imgui.h"
#include <GL/gl.h>
#include <algorithm>
#include <cmath>

namespace ui {

namespace {
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
using gfx::ToViewerSpace;
} // namespace

void TmdPanel::LoadFile(const std::string& path, const tim::Document& document) {
    // With nothing loaded to texture against yet, default to showing plain
    // shading/color instead of whatever happens to be sitting in
    // (probably blank) VRAM - only on the very first TMD, so toggling
    // "Textured" back on by hand later isn't silently undone by loading a
    // second file.
    bool is_first_model = doc.Models().empty();

    if (!doc.LoadFile(path)) return;

    if (is_first_model && document.Images().empty()) textured = false;
    FrameSelection();
}

void TmdPanel::NewModel() {
    std::string path = FileDialog::SaveFile("New TMD file", { { "TMD Files", "tmd" } });
    if (path.empty()) return;
    doc.NewModelAt(path);
}

void TmdPanel::Render(tim::Document& document, VRAMManager& vram_manager) {
    if (last_vram_version != document.GetVramVersion()) {
        vram_manager.RebuildFromImages(document.Images());
        texture_cache.InvalidateAll();
        last_vram_version = document.GetVramVersion();
    }

    if (HasActiveModel()) {
        int model_index = doc.ActiveModel();
        tmd::TMD_Model& active = doc.Models()[model_index].model;
        export_dialog.Render(active, vram_manager);

        std::string imported_path;
        gfx::TmdObjImport::ImportReport import_report;
        if (import_dialog.Render(active, imported_path, import_report)) {
            for (const auto& r : import_report.objects) {
                if (r.exists_in_model) PushUndo(model_index, r.object_index);
            }
            gfx::TmdObjImport::Apply(imported_path, active);
            doc.Models()[model_index].dirty = true;
            FrameSelection(); // geometry may have moved a lot; keep it in view
        }
    }

    ImGui::BeginChild("TmdSidebar", ImVec2(sidebar_width, 0), true);
    RenderSidebar(document);
    ImGui::EndChild();

    sidebar_width += ui::VerticalSplitter("TmdSplitter");
    sidebar_width = std::clamp(sidebar_width, 160.0f, 480.0f);

    ImGui::BeginChild("TmdRight", ImVec2(0, 0), false);
    ImGui::BeginChild("TmdFileBar", ImVec2(0, kFileBarHeight), true);
    RenderFileBar();
    ImGui::EndChild();

    RenderMainArea(document, vram_manager);
    ImGui::EndChild();
}

void TmdPanel::OpenExportDialog() {
    if (!HasActiveModel()) return;
    const tmd::TMD_Model& active = doc.Models()[doc.ActiveModel()].model;
    export_dialog.Open(active.filename, static_cast<int>(active.objects.size()), doc.ActiveObject());
}

void TmdPanel::OpenImportDialog() {
    if (!HasActiveModel()) return;
    import_dialog.Open();
}

void TmdPanel::RenderMainArea(const tim::Document& document, VRAMManager& vram_manager) {
    if (ImGui::BeginTabBar("TmdMainTabs")) {
        if (ImGui::BeginTabItem("3D View")) {
            ImGui::BeginChild("TmdInfoBar", ImVec2(0, kInfoBarHeight), true);
            RenderInfoBar();
            ImGui::EndChild();
            ImGui::BeginChild("TmdViewport", ImVec2(0, 0), true,
                               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            RenderViewport(vram_manager);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Model Editor")) {
            if (doc.HasActiveObject()) {
                int model_index = doc.ActiveModel();
                int object_index = doc.ActiveObject();
                model_editor.Render(
                    model_index, object_index, doc.Models()[model_index].model.objects[object_index], vram_manager,
                    texture_cache, [this, model_index, object_index]() { PushUndo(model_index, object_index); },
                    [this, model_index]() { doc.Models()[model_index].dirty = true; });
            } else {
                ImGui::TextDisabled("Select an object in the sidebar to edit its mesh.");
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Raw Editor")) {
            if (doc.HasActiveObject()) {
                int model_index = doc.ActiveModel();
                int object_index = doc.ActiveObject();
                raw_editor.Render(
                    model_index, object_index, doc.Models()[model_index].model.objects[object_index], document,
                    vram_manager, texture_cache,
                    [this, model_index, object_index]() { PushUndo(model_index, object_index); },
                    [this, model_index]() { doc.Models()[model_index].dirty = true; });
            } else {
                ImGui::TextDisabled("Select an object in the sidebar to inspect its raw fields.");
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void TmdPanel::RenderSidebar(const tim::Document& document) {
    if (ImGui::Button("New TMD...", ImVec2(-1, 0))) NewModel();
    if (ImGui::Button("Open TMD...", ImVec2(-1, 0))) {
        auto selection = FileDialog::OpenFiles("Select TMD files", { { "TMD Files", "tmd" } });
        for (const auto& path : selection) LoadFile(path, document);
    }

    ImGui::BeginDisabled(!HasActiveModel());
    if (ImGui::Button("Export Model...", ImVec2(-1, 0))) OpenExportDialog();
    if (ImGui::Button("Import Model...", ImVec2(-1, 0))) OpenImportDialog();
    ImGui::EndDisabled();
    ImGui::Separator();

    int close_index = -1;
    auto& models = doc.Models();
    for (int m = 0; m < static_cast<int>(models.size()); m++) {
        auto& loaded = models[m];
        std::string filename = loaded.model.filename.substr(loaded.model.filename.find_last_of("/\\") + 1);

        bool all_visible = true;
        for (bool v : loaded.visible) all_visible &= v;

        ImGui::PushID(m);
        // The checkbox toggles which objects render (like TIM's per-image
        // checkboxes); it's independent of clicking the label/tree, which
        // just picks which object's stats/transform show in the info bar.
        if (ImGui::Checkbox("##model_chk", &all_visible)) {
            for (size_t i = 0; i < loaded.visible.size(); i++) loaded.visible[i] = all_visible;
        }
        ImGui::SameLine();

        std::string header = filename + (loaded.dirty ? " *" : "") + "  (" +
                              std::to_string(loaded.model.objects.size()) + " object" +
                              (loaded.model.objects.size() == 1 ? ")" : "s)");

        // GetWindowContentRegionMax() (unlike GetWindowWidth()) already
        // excludes a visible scrollbar's width, so the close button lands
        // to its left instead of underneath it once the tree overflows.
        // The label is truncated to never reach that column in the first
        // place - an unframed TreeNodeEx's clickable area follows its
        // rendered text width, and since overlapping ImGui items resolve
        // first-submitted-wins, a long label would otherwise swallow
        // clicks meant for the button drawn after it.
        float close_btn_x = ImGui::GetWindowContentRegionMax().x - 22.0f;
        float label_start_x = ImGui::GetCursorPosX();
        const ImGuiStyle& style = ImGui::GetStyle();
        float max_label_width = close_btn_x - label_start_x - ImGui::GetFontSize() - style.FramePadding.x * 2 -
                                 style.ItemSpacing.x * 2 - 4.0f;
        header = TruncateToWidth(header, max_label_width);

        bool node_open = ImGui::TreeNodeEx("##model_node", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow,
                                            "%s", header.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", filename.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            doc.SetActive(m, -1);
            FrameSelection();
        }
        ImGui::SameLine();
        ImGui::SetCursorPosX(close_btn_x);
        if (ImGui::SmallButton("x")) {
            if (loaded.dirty) {
                pending_close_index = m;
                open_close_confirm = true;
            } else {
                close_index = m;
            }
        }

        if (node_open) {
            for (int o = 0; o < static_cast<int>(loaded.model.objects.size()); o++) {
                ImGui::PushID(o);
                bool object_visible = loaded.visible[o];
                if (ImGui::Checkbox("##obj_chk", &object_visible)) loaded.visible[o] = object_visible;
                ImGui::SameLine();

                bool selected = (doc.ActiveModel() == m && doc.ActiveObject() == o);
                std::string label = "Object " + std::to_string(o);
                if (ImGui::Selectable(label.c_str(), selected)) {
                    doc.SetActive(m, o);
                    FrameSelection();
                }
                ImGui::PopID();
            }

            // A new submodel: TMD_Object{} default-constructs to a
            // perfectly valid empty object (0 verts/normals/polygons) -
            // there's nothing else to fill in before it can be selected
            // and built up from scratch in the Model Editor tab.
            if (ImGui::Selectable("+ Add Object")) doc.AddObject(m);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    if (close_index >= 0) doc.CloseModel(close_index);

    RenderCloseConfirmPopup();
}

void TmdPanel::RenderCloseConfirmPopup() {
    if (open_close_confirm) {
        ImGui::OpenPopup("Close TMD File?");
        open_close_confirm = false;
    }

    if (!ImGui::BeginPopupModal("Close TMD File?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    auto& models = doc.Models();
    if (pending_close_index < 0 || pending_close_index >= static_cast<int>(models.size())) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    LoadedModel& loaded = models[pending_close_index];
    std::string filename = loaded.model.filename.substr(loaded.model.filename.find_last_of("/\\") + 1);
    ImGui::Text("\"%s\" has unsaved changes.", filename.c_str());
    ImGui::Text("Save changes before closing?");
    ImGui::Separator();

    auto close_it = [&]() {
        doc.CloseModel(pending_close_index);
        pending_close_index = -1;
    };

    if (ImGui::Button("Save")) {
        tmd::Writer::WriteToFile(loaded.model.filename, loaded.model);
        close_it();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard")) {
        close_it();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        pending_close_index = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void TmdPanel::RenderFileBar() {
    bool has_model = doc.HasActiveModel();

    // The active file's path/dirty state/Save, matching the TIM Editor's
    // own per-image header (RenderPreview in inspector_panel.cpp) so both
    // editors present a file the same way. Model-level state (which file,
    // whether it's saved, undo history) applies no matter which of the
    // three sub-tabs below is open, so this stays above all of them rather
    // than living inside just one - unlike the render toggles/camera/
    // transform info bar, which really is 3D-View-only (see RenderInfoBar).
    if (has_model) {
        LoadedModel& loaded = doc.Models()[doc.ActiveModel()];
        ImGui::TextUnformatted(loaded.model.filename.c_str());
        if (loaded.dirty) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "(unsaved changes)");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!loaded.dirty);
        if (ImGui::SmallButton("Save")) SaveActiveModel();
        ImGui::EndDisabled();
    } else {
        ImGui::TextDisabled("Open a .tmd file to begin.");
    }

    // A visible Undo button, not just the Ctrl+Z shortcut: EditorApp skips
    // that shortcut whenever a text/numeric field has focus (so it doesn't
    // fight with the field's own text-undo), which after editing a Raw
    // Editor field is most of the time - a keyboard-only undo would be
    // easy to think "isn't working". Undo applies to whichever object was
    // last edited regardless of tab, so it belongs here, not in "3D View".
    ImGui::SameLine(0, 16.0f);
    ImGui::BeginDisabled(!CanUndo());
    if (ImGui::Button("Undo")) Undo();
    ImGui::EndDisabled();
}

void TmdPanel::RenderInfoBar() {
    bool has_model = doc.HasActiveModel();
    bool has_object = doc.HasActiveObject();

    // Row 0: whichever stats currently apply, plus the render toggles - all
    // on one line so the bar stays a fixed, small height. These (and the
    // transform row below) are specific to this tab's own viewport/camera,
    // unlike the file/Undo bar above the tab strip - see RenderFileBar.
    if (has_model && !has_object) {
        const auto& loaded = doc.Models()[doc.ActiveModel()];
        size_t verts = 0, norms = 0, polys = 0;
        for (const auto& obj : loaded.model.objects) {
            verts += obj.vertices.size();
            norms += obj.normals.size();
            polys += obj.polygons.size();
        }
        ImGui::Text("%d obj, %zu verts, %zu polys", static_cast<int>(loaded.model.objects.size()), verts, polys);
    } else if (has_object) {
        int active_object = doc.ActiveObject();
        const auto& obj = doc.Models()[doc.ActiveModel()].model.objects[active_object];
        ImGui::Text("Obj %d: %zu verts, %zu polys", active_object, obj.vertices.size(), obj.polygons.size());
    }

    ImGui::SameLine(0, 16.0f);
    ImGui::Checkbox("Textured", &textured);
    ImGui::SameLine();
    ImGui::Checkbox("Wireframe", &wireframe);
    ImGui::SameLine();
    ImGui::Checkbox("Cull Backfaces", &cull_backfaces);
    ImGui::SameLine();
    if (ImGui::Button("Reset Camera")) FrameSelection();

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Drag to orbit, scroll to zoom, WASD+Q/E to fly while hovering the viewport.");
    }

    // Row 1: the active object's transform, or a hint when none is picked -
    // always present so the bar's height never jumps between the two.
    if (has_object) {
        ObjectTransform& xf = doc.Models()[doc.ActiveModel()].transforms[doc.ActiveObject()];
        ImGui::SetNextItemWidth(180.0f);
        ImGui::DragFloat3("Pos", xf.pos, 1.0f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        ImGui::DragFloat3("Rot", xf.rot_deg, 1.0f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::DragFloat("Scale", &xf.scale, 0.01f, 0.01f, 100.0f);
    } else {
        ImGui::TextDisabled("Select an object in the sidebar to edit its transform.");
    }
}

void TmdPanel::FrameSelection() {
    if (!doc.HasActiveModel()) return;
    const auto& loaded = doc.Models()[doc.ActiveModel()];
    int active_object = doc.ActiveObject();

    gfx::Vec3 lo{ 1e9f, 1e9f, 1e9f }, hi{ -1e9f, -1e9f, -1e9f };
    bool any = false;

    auto consider = [&](const tmd::TMD_Object& obj, const ObjectTransform& xf) {
        gfx::Mat4 model_matrix = BuildModelMatrix(xf);
        for (const auto& v : obj.vertices) {
            gfx::Vec3 p = gfx::TransformPoint(model_matrix, ToViewerSpace(v.x, v.y, v.z));
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
            hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
            any = true;
        }
    };

    if (active_object < 0) {
        for (int o = 0; o < static_cast<int>(loaded.model.objects.size()); o++) {
            if (loaded.visible[o]) consider(loaded.model.objects[o], loaded.transforms[o]);
        }
    } else if (active_object < static_cast<int>(loaded.model.objects.size())) {
        consider(loaded.model.objects[active_object], loaded.transforms[active_object]);
    }

    if (!any) return;

    cam_target = (lo + hi) * 0.5f;
    float extent = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1.0f });
    cam_distance = extent * 1.6f;
    cam_yaw = 0.6f;
    cam_pitch = 0.35f;
}

gfx::Mat4 TmdPanel::BuildModelMatrix(const ObjectTransform& t) const {
    gfx::Mat4 translate = gfx::Mat4::Translate({ t.pos[0], t.pos[1], t.pos[2] });
    gfx::Mat4 rot = gfx::Mat4::RotateY(t.rot_deg[1] * kDegToRad) * gfx::Mat4::RotateX(t.rot_deg[0] * kDegToRad) *
                    gfx::Mat4::RotateZ(t.rot_deg[2] * kDegToRad);
    gfx::Mat4 scale = gfx::Mat4::Scale({ t.scale, t.scale, t.scale });
    return translate * rot * scale;
}

void TmdPanel::HandleCameraInput(bool hovered, bool dragging) {
    ImGuiIO& io = ImGui::GetIO();

    // Gated on the invisible button being *active* rather than hovered, so
    // an orbit drag that started over the viewport keeps tracking the mouse
    // even if it briefly leaves the viewport bounds mid-drag.
    if (dragging) {
        cam_yaw -= io.MouseDelta.x * 0.01f;
        cam_pitch = std::clamp(cam_pitch - io.MouseDelta.y * 0.01f, -1.5f, 1.5f);
    }
    if (hovered && io.MouseWheel != 0.0f) {
        cam_distance = std::clamp(cam_distance * (1.0f - io.MouseWheel * 0.1f), 1.0f, 1000000.0f);
    }

    if (!hovered) return;

    // The eye-to-target look direction (negative of the eye's offset from
    // target below, since that offset points outward from target to eye).
    gfx::Vec3 forward{ -std::cos(cam_pitch) * std::sin(cam_yaw), -std::sin(cam_pitch),
                        -std::cos(cam_pitch) * std::cos(cam_yaw) };
    gfx::Vec3 right = forward.Cross({ 0, 1, 0 }).Normalized();
    float speed = cam_distance * 0.8f * io.DeltaTime;

    if (ImGui::IsKeyDown(ImGuiKey_W)) cam_target = cam_target + forward * speed;
    if (ImGui::IsKeyDown(ImGuiKey_S)) cam_target = cam_target - forward * speed;
    if (ImGui::IsKeyDown(ImGuiKey_A)) cam_target = cam_target - right * speed;
    if (ImGui::IsKeyDown(ImGuiKey_D)) cam_target = cam_target + right * speed;
    if (ImGui::IsKeyDown(ImGuiKey_E)) cam_target.y += speed;
    if (ImGui::IsKeyDown(ImGuiKey_Q)) cam_target.y -= speed;
}

void TmdPanel::RenderViewport(VRAMManager& vram_manager) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1 || avail.y < 1) return;
    framebuffer.EnsureSize(static_cast<int>(avail.x), static_cast<int>(avail.y));

    ImVec2 image_pos = ImGui::GetCursorScreenPos();
    // The FBO's texture origin is bottom-left (standard GL), ImGui's is
    // top-left, hence the flipped V coordinates here.
    ImGui::Image((void*)(intptr_t)framebuffer.ColorTexture(), avail, ImVec2(0, 1), ImVec2(1, 0));
    ImGui::SetCursorScreenPos(image_pos);
    ImGui::InvisibleButton("TmdViewportInput", avail);
    bool hovered = ImGui::IsItemHovered();
    bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    HandleCameraInput(hovered, dragging);

    gfx::Vec3 eye{ cam_target.x + cam_distance * std::cos(cam_pitch) * std::sin(cam_yaw),
                   cam_target.y + cam_distance * std::sin(cam_pitch),
                   cam_target.z + cam_distance * std::cos(cam_pitch) * std::cos(cam_yaw) };
    gfx::Mat4 view = gfx::Mat4::LookAt(eye, cam_target, { 0, 1, 0 });
    gfx::Mat4 proj = gfx::Mat4::Perspective(60.0f * kDegToRad, avail.x / avail.y, 1.0f, 1000000.0f);

    framebuffer.Bind();
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.m);

    if (doc.HasActiveModel()) {
        DrawModel(doc.Models()[doc.ActiveModel()], vram_manager);
    }

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // wireframe must never leak into ImGui's own rendering
    framebuffer.Unbind();
}

void TmdPanel::DrawModel(const LoadedModel& loaded, VRAMManager& vram_manager) {
    // Renders every checked object (see the sidebar's per-object checkbox),
    // independent of which one is "active" for the info bar's transform
    // editor below.
    for (int o = 0; o < static_cast<int>(loaded.model.objects.size()); o++) {
        if (loaded.visible[o]) DrawObject(loaded.model.objects[o], loaded.transforms[o], vram_manager);
    }
}

void TmdPanel::DrawObject(const tmd::TMD_Object& obj, const ObjectTransform& xform, VRAMManager& vram_manager) {
    gfx::Mat4 model_matrix = BuildModelMatrix(xform);
    gfx::TmdObjectRenderer::Options options;
    options.textured = textured;
    options.cull_backfaces = cull_backfaces;
    options.wireframe = wireframe;
    gfx::TmdObjectRenderer::DrawObject(obj, model_matrix, vram_manager, texture_cache, options);
}

} // namespace ui
