// Verifies gfx::TmdGltfExport/TmdObjImport (which also handles glTF re-
// import) round-trip fidelity. Unlike the OBJ round-trip, glTF has no quad
// primitive, so every original quad exports as 2 triangles - the check
// below expects a triangle count, not a 1:1 polygon match, and looks for
// orphan vertices/bad flags/wrong mode instead.
#include "core/tmd_format.h"
#include "core/vram_manager.h"
#include "gfx/tmd_gltf_export.h"
#include "gfx/tmd_obj_import.h"
#include "support/synthetic_tmd.h"
#include <array>
#include <cstdio>
#include <vector>

int main() {
    tmd::TMD_Model model = BuildSyntheticTmdModel();
    VRAMManager vram;

    std::vector<int> object_indices;
    for (int i = 0; i < static_cast<int>(model.objects.size()); i++) object_indices.push_back(i);

    const char* out_dir = ".";
    gfx::TmdGltfExport::Options opts;
    opts.bake_textures = false;
    opts.include_colors = true;
    bool ok = gfx::TmdGltfExport::Export(model, object_indices, vram, out_dir, "gltf_roundtrip_tmp", opts);
    printf("Export ok=%d\n", ok);
    if (!ok) return 1;

    std::string path = std::string(out_dir) + "/gltf_roundtrip_tmp.gltf";

    auto report = gfx::TmdObjImport::Analyze(path, model);
    printf("Analyze ok=%d error='%s' objects=%zu\n", report.ok, report.error.c_str(), report.objects.size());
    if (!report.ok) return 1;

    tmd::TMD_Model rebuilt = model;
    auto apply_report = gfx::TmdObjImport::Apply(path, rebuilt);
    printf("Apply ok=%d\n", apply_report.ok);

    int total_polys = 0, textured = 0, bad_flags = 0, non_tri = 0, orphan_vertex = 0;
    int expected_tri_count_total = 0, actual_tri_count_total = 0;
    for (int oi : object_indices) {
        auto& orig = model.objects[oi];
        auto& reb = rebuilt.objects[oi];

        int expected_tris = 0;
        for (auto& op : orig.polygons) expected_tris += (op.num_verts == 4) ? 2 : 1;
        expected_tri_count_total += expected_tris;
        actual_tri_count_total += static_cast<int>(reb.polygons.size());
        if (expected_tris != static_cast<int>(reb.polygons.size())) {
            printf("obj %d: expected %d triangles, rebuilt has %zu polygons\n", oi, expected_tris,
                   reb.polygons.size());
        }

        std::vector<std::array<int16_t, 3>> orig_positions;
        for (auto& v : orig.vertices) orig_positions.push_back({ v.x, v.y, v.z });

        for (auto& rp : reb.polygons) {
            total_polys++;
            if (rp.num_verts != 3) non_tri++;
            if (rp.gouraud || rp.no_light || rp.double_sided || rp.semi_transparent || rp.color_per_vertex) {
                bad_flags++;
            }
            if (rp.textured) {
                textured++;
                if (rp.tsb != 0 || rp.cba != 0) bad_flags++;
            }
            for (int k = 0; k < rp.num_verts; k++) {
                auto& rv = reb.vertices[rp.vert_idx[k]];
                std::array<int16_t, 3> pos = { rv.x, rv.y, rv.z };
                bool found = false;
                for (auto& op : orig_positions) {
                    if (op == pos) { found = true; break; }
                }
                if (!found) orphan_vertex++;
            }
        }
    }
    printf("expected_tris=%d actual_tris=%d total_polys=%d textured=%d bad_flags=%d non_tri=%d orphan_vertex=%d\n",
           expected_tri_count_total, actual_tri_count_total, total_polys, textured, bad_flags, non_tri,
           orphan_vertex);

    return (bad_flags > 0 || non_tri > 0 || orphan_vertex > 0 || expected_tri_count_total != actual_tri_count_total)
               ? 1
               : 0;
}
