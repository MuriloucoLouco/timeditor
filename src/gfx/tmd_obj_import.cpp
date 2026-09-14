#include "tmd_obj_import.h"
#include "tmd_gltf_import.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace gfx::TmdObjImport {

namespace {

struct ParsedFace {
    int object_index = -1;
    std::vector<int> v, vt, vn; // 0-based; vt/vn entries are -1 if absent
};

struct ParsedFile {
    std::vector<MeshVec3> positions;
    std::vector<bool> position_has_color;
    std::vector<MeshVec3> position_color;
    std::vector<MeshVec2> uvs;
    std::vector<MeshVec3> normals;
    std::vector<ParsedFace> faces;
};

// v, v/vt, v//vn, or v/vt/vn - positive absolute indices only (Blender
// always writes these; relative/negative OBJ indices aren't supported).
void ParseFaceCorner(const std::string& tok, int& v, int& vt, int& vn) {
    v = vt = vn = -1;
    size_t p1 = tok.find('/');
    if (p1 == std::string::npos) {
        v = std::stoi(tok) - 1;
        return;
    }
    v = std::stoi(tok.substr(0, p1)) - 1;
    size_t p2 = tok.find('/', p1 + 1);
    if (p2 == std::string::npos) {
        std::string vt_str = tok.substr(p1 + 1);
        if (!vt_str.empty()) vt = std::stoi(vt_str) - 1;
        return;
    }
    std::string vt_str = tok.substr(p1 + 1, p2 - p1 - 1);
    if (!vt_str.empty()) vt = std::stoi(vt_str) - 1;
    std::string vn_str = tok.substr(p2 + 1);
    if (!vn_str.empty()) vn = std::stoi(vn_str) - 1;
}

bool ParseObjFile(const std::string& path, ParsedFile& out) {
    std::ifstream f(path);
    if (!f) return false;

    int current_object = -1;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        std::string tag;
        if (!(iss >> tag) || tag.empty() || tag[0] == '#') continue;

        if (tag == "v") {
            MeshVec3 v;
            iss >> v.x >> v.y >> v.z;
            out.positions.push_back(v);
            // Common (non-standard but widely supported) extension: "v x y
            // z r g b" for per-vertex color, as written by this app's own
            // exporter's "Include flat colors" option.
            MeshVec3 color;
            bool has_color = static_cast<bool>(iss >> color.x >> color.y >> color.z);
            out.position_has_color.push_back(has_color);
            out.position_color.push_back(has_color ? color : MeshVec3{});
        } else if (tag == "vt") {
            MeshVec2 v;
            iss >> v.x >> v.y;
            out.uvs.push_back(v);
        } else if (tag == "vn") {
            MeshVec3 v;
            iss >> v.x >> v.y >> v.z;
            out.normals.push_back(v);
        } else if (tag == "o") {
            std::string name;
            iss >> name;
            current_object = ParseTmdObjectIndex(name);
        } else if (tag == "f") {
            ParsedFace face;
            face.object_index = current_object;
            std::string tok;
            while (iss >> tok) {
                int v, vt, vn;
                ParseFaceCorner(tok, v, vt, vn);
                face.v.push_back(v);
                face.vt.push_back(vt);
                face.vn.push_back(vn);
            }
            if (face.v.size() >= 3) out.faces.push_back(std::move(face));
        }
    }
    return true;
}

// Groups parsed faces by object, splitting any face with more than 4
// corners into tri/quad-sized pieces via SplitFaceCorners.
std::vector<GenericMeshObject> BuildGenericMeshObjects(const ParsedFile& parsed, std::map<int, int>& ngon_splits) {
    std::map<int, GenericMeshObject> by_object;

    for (const auto& face : parsed.faces) {
        GenericMeshObject& mesh = by_object[face.object_index];
        mesh.object_index = face.object_index;

        std::vector<MeshCorner> corners;
        for (size_t i = 0; i < face.v.size(); i++) {
            corners.push_back({ face.v[i], face.vt[i], face.vn[i] });
        }

        auto groups = SplitFaceCorners(corners);
        if (groups.size() > 1) ngon_splits[face.object_index]++;

        for (const auto& group : groups) {
            GenericFace gf;
            gf.num_verts = static_cast<int>(group.size());
            for (int i = 0; i < gf.num_verts; i++) gf.corners[i] = group[i];

            // A face has no natural single color of its own in OBJ (colors
            // are per-vertex, via the extended `v` line) - average
            // whichever of its corners' source vertices carry one.
            MeshVec3 sum{ 0, 0, 0 };
            int count = 0;
            for (int i = 0; i < gf.num_verts; i++) {
                int pi = group[i].position_idx;
                if (pi >= 0 && pi < static_cast<int>(parsed.position_has_color.size()) &&
                    parsed.position_has_color[pi]) {
                    sum.x += parsed.position_color[pi].x;
                    sum.y += parsed.position_color[pi].y;
                    sum.z += parsed.position_color[pi].z;
                    count++;
                }
            }
            if (count > 0) {
                gf.has_color = true;
                gf.color = { sum.x / count, sum.y / count, sum.z / count };
            }

            mesh.faces.push_back(gf);
        }
    }

    for (auto& [index, mesh] : by_object) {
        mesh.positions = parsed.positions;
        mesh.uvs = parsed.uvs;
        mesh.normals = parsed.normals;
    }

    std::vector<GenericMeshObject> result;
    for (auto& [index, mesh] : by_object) result.push_back(std::move(mesh));
    return result;
}

bool IsGltfPath(const std::string& path) {
    auto ends_with = [&](const char* ext) {
        size_t n = std::strlen(ext);
        return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
    };
    return ends_with(".gltf") || ends_with(".glb") || ends_with(".GLTF") || ends_with(".GLB");
}

} // namespace

ImportReport Analyze(const std::string& model_path, const tmd::TMD_Model& model) {
    if (IsGltfPath(model_path)) return TmdGltfImport::Analyze(model_path, model);

    ImportReport report;
    ParsedFile parsed;
    if (!ParseObjFile(model_path, parsed)) {
        report.error = "Could not open " + model_path;
        return report;
    }

    std::map<int, int> ngon_splits;
    auto meshes = BuildGenericMeshObjects(parsed, ngon_splits);
    // ngon_splits is keyed by the *pre-resolution* object index (possibly
    // -1) since it's tallied while still parsing, before any name-less
    // group gets a geometry-guessed index - look it up before resolving.
    std::vector<int> pre_resolve_ngon_splits(meshes.size());
    for (size_t i = 0; i < meshes.size(); i++) {
        pre_resolve_ngon_splits[i] = ngon_splits.count(meshes[i].object_index) ? ngon_splits[meshes[i].object_index] : 0;
    }
    auto matched_by_geometry = ResolveUnmatchedObjects(meshes, model);
    for (size_t i = 0; i < meshes.size(); i++) {
        const auto& mesh = meshes[i];
        MeshImportStats stats;
        stats.object_index = mesh.object_index;
        stats.exists_in_model = mesh.object_index >= 0 && mesh.object_index < static_cast<int>(model.objects.size());
        stats.matched_by_geometry = matched_by_geometry[i];
        stats.face_count = static_cast<int>(mesh.faces.size());
        stats.vertex_count = static_cast<int>(mesh.positions.size());
        stats.ngon_splits = pre_resolve_ngon_splits[i];
        report.objects.push_back(stats);
    }
    report.ok = true;
    return report;
}

ImportReport Apply(const std::string& model_path, tmd::TMD_Model& model) {
    if (IsGltfPath(model_path)) return TmdGltfImport::Apply(model_path, model);

    ImportReport report;
    ParsedFile parsed;
    if (!ParseObjFile(model_path, parsed)) {
        report.error = "Could not open " + model_path;
        return report;
    }

    std::map<int, int> ngon_splits;
    auto meshes = BuildGenericMeshObjects(parsed, ngon_splits);
    std::vector<int> pre_resolve_ngon_splits(meshes.size());
    for (size_t i = 0; i < meshes.size(); i++) {
        pre_resolve_ngon_splits[i] = ngon_splits.count(meshes[i].object_index) ? ngon_splits[meshes[i].object_index] : 0;
    }
    auto matched_by_geometry = ResolveUnmatchedObjects(meshes, model);
    for (size_t i = 0; i < meshes.size(); i++) {
        auto& mesh = meshes[i];
        MeshImportStats stats;
        stats.object_index = mesh.object_index;
        stats.exists_in_model = mesh.object_index >= 0 && mesh.object_index < static_cast<int>(model.objects.size());
        stats.matched_by_geometry = matched_by_geometry[i];
        stats.ngon_splits = pre_resolve_ngon_splits[i];

        if (stats.exists_in_model) {
            model.objects[mesh.object_index] = RebuildTmdObject(mesh, stats);
        }
        report.objects.push_back(stats);
    }
    report.ok = true;
    return report;
}

} // namespace gfx::TmdObjImport
