#include "model_import_dialog.h"
#include "file_dialog.h"
#include "imgui.h"
#include <cstring>

namespace ui {

void ModelImportDialog::Open() {
    should_open = true;
    analyzed = false;
    report = {};
}

bool ModelImportDialog::Render(const tmd::TMD_Model& model, std::string& out_path,
                                gfx::TmdObjImport::ImportReport& out_report) {
    if (should_open) {
        ImGui::OpenPopup("Import Model");
        should_open = false;
    }

    bool applied = false;

    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal("Import Model", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return false;

    ImGui::TextWrapped("Pick a .obj or .gltf/.glb (exported earlier via \"Export Model...\", and possibly "
                        "edited externally since). This replaces each matching object's vertices, normals, "
                        "and primitives entirely, straight from the mesh - untextured faces without their "
                        "own color come back as placeholder purple; fix up texture references and other "
                        "PS1-specific fields afterward in the Raw Editor.");
    ImGui::Separator();

    ImGui::InputText("##modelpath", model_path, sizeof(model_path));
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        std::string path = FileDialog::OpenFile("Select edited model file",
                                                 { { "3D Model Files", "obj,gltf,glb" } });
        if (!path.empty()) {
            std::strncpy(model_path, path.c_str(), sizeof(model_path) - 1);
            model_path[sizeof(model_path) - 1] = '\0';
            report = gfx::TmdObjImport::Analyze(model_path, model);
            analyzed = true;
        }
    }

    ImGui::Separator();

    if (analyzed) {
        if (!report.ok) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", report.error.c_str());
        } else if (report.objects.empty()) {
            ImGui::TextDisabled("No faces found in that file.");
        } else {
            for (const auto& r : report.objects) {
                if (!r.exists_in_model) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                                        "Object %d: not present in the loaded model - skipped.", r.object_index);
                    continue;
                }
                ImGui::Text("Object %d: %d faces, %d vertices", r.object_index, r.face_count, r.vertex_count);
                if (r.matched_by_geometry) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "(matched by shape, not name)");
                }
                if (r.ngon_splits > 0) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%d n-gon%s split into tris/quads)", r.ngon_splits,
                                         r.ngon_splits == 1 ? "" : "s");
                }
            }
        }
    } else {
        ImGui::TextDisabled("Pick a file to see a preview of what will change.");
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                        "This overwrites geometry in the loaded model and isn't undoable across a "
                        "restart - re-export first if you want a backup.");

    bool any_applicable = false;
    if (analyzed && report.ok) {
        for (const auto& r : report.objects) any_applicable |= r.exists_in_model;
    }

    ImGui::BeginDisabled(!any_applicable);
    if (ImGui::Button("Apply")) {
        out_path = model_path;
        out_report = report;
        applied = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    return applied;
}

} // namespace ui
