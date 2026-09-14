#include "model_export_dialog.h"
#include "file_dialog.h"
#include "../gfx/tmd_obj_export.h"
#include "../gfx/tmd_gltf_export.h"
#include "imgui.h"
#include <cstring>

namespace ui {

namespace {
std::string BaseNameNoExt(const std::string& path) {
    std::string name = path.substr(path.find_last_of("/\\") + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return name;
}
} // namespace

void ModelExportDialog::Open(const std::string& tmd_filename, int object_count_, int active_object) {
    should_open = true;
    status_message.clear();
    object_count = object_count_;
    this_object_index = active_object;
    scope = active_object >= 0 ? 0 : 1;

    std::string base = BaseNameNoExt(tmd_filename);
    std::strncpy(base_name, base.c_str(), sizeof(base_name) - 1);
    base_name[sizeof(base_name) - 1] = '\0';
}

void ModelExportDialog::Render(const tmd::TMD_Model& model, const VRAMManager& vram_manager) {
    if (should_open) {
        ImGui::OpenPopup("Export Model");
        should_open = false;
    }

    ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal("Export Model", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::TextWrapped("Writes a mesh bundle you can edit UVs/geometry on externally, "
                        "then bring back with \"Import Model...\".");
    ImGui::Separator();

    ImGui::BeginDisabled(this_object_index < 0);
    ImGui::RadioButton(("This Object (#" + std::to_string(this_object_index) + ")").c_str(), &scope, 0);
    ImGui::EndDisabled();
    ImGui::RadioButton(("Whole File (" + std::to_string(object_count) + " objects)").c_str(), &scope, 1);

    ImGui::Spacing();
    const char* formats[] = { "Wavefront OBJ", "glTF" };
    ImGui::SetNextItemWidth(160.0f);
    ImGui::Combo("Format", &format, formats, 2);

    ImGui::Spacing();
    ImGui::Checkbox("Bake reference textures", &bake_textures);
    ImGui::BeginDisabled(!bake_textures);
    ImGui::Checkbox("Pack into a single atlas image", &pack_atlas);
    ImGui::EndDisabled();
    if (bake_textures && pack_atlas) {
        ImGui::TextDisabled("All textured materials share one combined PNG instead of one each.");
    }
    ImGui::Checkbox("Include flat colors", &include_colors);
    if (!include_colors) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                            "Untextured parts will reimport as placeholder purple unless you set their "
                            "color again by hand.");
    }

    ImGui::Spacing();
    ImGui::InputText("Bundle Name", base_name, sizeof(base_name));

    ImGui::InputText("##outdir", output_dir, sizeof(output_dir));
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        std::string dir = FileDialog::PickFolder("Choose export folder", output_dir);
        if (!dir.empty()) {
            std::strncpy(output_dir, dir.c_str(), sizeof(output_dir) - 1);
            output_dir[sizeof(output_dir) - 1] = '\0';
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Output folder");

    const char* ext = format == 0 ? "obj" : "gltf";
    const char* aux = format == 0 ? "mtl" : "bin";
    const char* texture_note =
        !bake_textures ? "" : pack_atlas ? ", and one combined atlas PNG" : ", and one PNG per texture used";
    ImGui::TextDisabled("Creates %s.%s, %s.%s%s in that folder.", base_name, ext, base_name, aux, texture_note);

    ImGui::Separator();
    if (!status_message.empty()) {
        ImGui::TextWrapped("%s", status_message.c_str());
        ImGui::Separator();
    }

    bool can_export = output_dir[0] != '\0' && base_name[0] != '\0' && (scope == 1 || this_object_index >= 0);
    ImGui::BeginDisabled(!can_export);
    if (ImGui::Button("Export")) {
        std::vector<int> object_indices;
        if (scope == 0) {
            object_indices = { this_object_index };
        } else {
            for (int i = 0; i < static_cast<int>(model.objects.size()); i++) object_indices.push_back(i);
        }

        bool ok;
        if (format == 0) {
            gfx::TmdObjExport::Options options;
            options.bake_textures = bake_textures;
            options.pack_atlas = pack_atlas;
            options.include_colors = include_colors;
            ok = gfx::TmdObjExport::ExportForEditing(model, object_indices, vram_manager, output_dir, base_name,
                                                      options);
        } else {
            gfx::TmdGltfExport::Options options;
            options.bake_textures = bake_textures;
            options.pack_atlas = pack_atlas;
            options.include_colors = include_colors;
            ok = gfx::TmdGltfExport::Export(model, object_indices, vram_manager, output_dir, base_name, options);
        }
        status_message = ok ? "Exported successfully to " + std::string(output_dir) : "Export failed.";
        if (ok) ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        status_message.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

} // namespace ui
