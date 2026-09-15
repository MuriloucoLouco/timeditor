// Verifies gfx::TmdObjExport/TmdObjImport round-trip fidelity: exporting a
// model to OBJ+MTL and immediately reimporting it (no edits in between).
//
// Reimport is documented (see tmd_obj_export.h's ExportForEditing comment)
// to always rebuild each polygon's shading mode fresh from the generic OBJ
// data rather than recovering the original tsb/cba/gouraud/no_light/
// double_sided/semi_transparent/color_per_vertex - "fix those up afterward
// with the Raw Editor" is the intended workflow, not a bug. Concretely,
// since this exporter always writes a UV (`vt`) index for every face
// whether or not it was textured, every rebuilt polygon comes back
// textured=true with tsb/cba reset to 0 (mirroring test_gltf_roundtrip.cpp's
// own, already-correct expectation of the same reset). What genuinely IS
// guaranteed - and what this test actually checks - is vertex positions and
// UV values surviving the round trip exactly.
#include "core/tmd_format.h"
#include "core/vram_manager.h"
#include "gfx/tmd_obj_export.h"
#include "gfx/tmd_obj_import.h"
#include "support/synthetic_tmd.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main() {
    tmd::TMD_Model model = BuildSyntheticTmdModel();
    VRAMManager vram; // never InitializeGL()'d - fine, DecodeTexPage/tile-width math never touches GL

    std::vector<int> object_indices;
    for (int i = 0; i < static_cast<int>(model.objects.size()); i++) object_indices.push_back(i);

    const char* out_dir = "."; // CMake runs tests with a scratch working directory - see tests/CMakeLists.txt
    gfx::TmdObjExport::Options opts;
    opts.bake_textures = false; // skip PNG baking, irrelevant to this structural test
    bool ok = gfx::TmdObjExport::ExportForEditing(model, object_indices, vram, out_dir, "obj_roundtrip_tmp", opts);
    printf("Export ok=%d\n", ok);
    if (!ok) return 1;

    std::string obj_path = std::string(out_dir) + "/obj_roundtrip_tmp.obj";

    auto report = gfx::TmdObjImport::Analyze(obj_path, model);
    printf("Analyze ok=%d error='%s'\n", report.ok, report.error.c_str());
    for (auto& r : report.objects) {
        printf("  obj %d: exists=%d matched_by_geometry=%d faces=%d verts=%d ngon_splits=%d\n", r.object_index,
               r.exists_in_model, r.matched_by_geometry, r.face_count, r.vertex_count, r.ngon_splits);
    }

    tmd::TMD_Model rebuilt_model = model;
    auto apply_report = gfx::TmdObjImport::Apply(obj_path, rebuilt_model);
    printf("Apply ok=%d\n", apply_report.ok);

    int total_mismatches = 0;
    for (int obj_index : object_indices) {
        const auto& orig = model.objects[obj_index];
        const auto& rebuilt = rebuilt_model.objects[obj_index];

        if (orig.polygons.size() != rebuilt.polygons.size()) {
            printf("obj %d: polygon count mismatch %zu vs %zu\n", obj_index, orig.polygons.size(),
                   rebuilt.polygons.size());
            total_mismatches++;
            continue;
        }

        for (size_t p = 0; p < orig.polygons.size(); p++) {
            const auto& po = orig.polygons[p];
            const auto& pr = rebuilt.polygons[p];

            if (po.num_verts != pr.num_verts) {
                printf("obj %d prim %zu: num_verts mismatch %d vs %d\n", obj_index, p, po.num_verts, pr.num_verts);
                total_mismatches++;
                continue;
            }

            // Every rebuilt polygon must have its mode reset to fresh-
            // import defaults, per the reimport contract described above.
            if (pr.gouraud || pr.no_light || pr.double_sided || pr.semi_transparent || pr.color_per_vertex) {
                printf("obj %d prim %zu: expected reset flags, got gr=%d nl=%d ds=%d st=%d cpv=%d\n", obj_index, p,
                       pr.gouraud, pr.no_light, pr.double_sided, pr.semi_transparent, pr.color_per_vertex);
                total_mismatches++;
            }
            if (!pr.textured || pr.tsb != 0 || pr.cba != 0) {
                printf("obj %d prim %zu: expected textured=1 tsb=0 cba=0 (this exporter always attaches UV), got "
                       "textured=%d tsb=%d cba=%d\n",
                       obj_index, p, pr.textured, pr.tsb, pr.cba);
                total_mismatches++;
            }

            // UV values ARE preserved byte-exact (within +/-1 for float
            // rounding at the byte boundary) for originally-textured faces.
            if (po.textured) {
                for (int k = 0; k < po.num_verts; k++) {
                    int du = std::abs(static_cast<int>(po.u[k]) - static_cast<int>(pr.u[k]));
                    int dv = std::abs(static_cast<int>(po.v[k]) - static_cast<int>(pr.v[k]));
                    if (du > 1 || dv > 1) {
                        printf("obj %d prim %zu corner %d: UV MISMATCH orig=(%d,%d) rebuilt=(%d,%d)\n", obj_index, p,
                               k, po.u[k], po.v[k], pr.u[k], pr.v[k]);
                        total_mismatches++;
                    }
                }
            }

            // Vertex positions are preserved exactly regardless of textured status.
            for (int k = 0; k < po.num_verts; k++) {
                auto& vo = orig.vertices[po.vert_idx[k]];
                auto& vr = rebuilt.vertices[pr.vert_idx[k]];
                if (vo.x != vr.x || vo.y != vr.y || vo.z != vr.z) {
                    printf("obj %d prim %zu corner %d: VERTEX MISMATCH orig=(%d,%d,%d) rebuilt=(%d,%d,%d)\n",
                           obj_index, p, k, vo.x, vo.y, vo.z, vr.x, vr.y, vr.z);
                    total_mismatches++;
                }
            }
        }
    }

    printf("TOTAL MISMATCHES: %d\n", total_mismatches);
    return total_mismatches > 0 ? 1 : 0;
}
