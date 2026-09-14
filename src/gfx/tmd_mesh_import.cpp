#include "tmd_mesh_import.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

namespace gfx {

namespace {

MeshVec3 Sub(MeshVec3 a, MeshVec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
MeshVec3 Cross(MeshVec3 a, MeshVec3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
MeshVec3 Normalize(MeshVec3 v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-6f) return { 0, 1, 0 };
    return { v.x / len, v.y / len, v.z / len };
}
MeshVec3 Add(MeshVec3 a, MeshVec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
MeshVec3 Scale(MeshVec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }

int16_t ClampRoundI16(float v) { return static_cast<int16_t>(std::clamp(std::lround(v), -32768L, 32767L)); }
uint8_t ClampByteU8(float v) { return static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L)); }

// A rough shape signature used to match an unnamed/renamed imported group
// against an existing TMD object: where its (referenced) vertices are
// centered, how big a box they span, and how many there are. Not a
// rigorous shape descriptor - just enough to tell genuinely different
// objects apart when names can't be trusted.
struct MeshFingerprint {
    MeshVec3 center{ 0, 0, 0 };
    MeshVec3 extent{ 0, 0, 0 };
    int vertex_count = 0;
};

MeshFingerprint ComputeMeshFingerprint(const GenericMeshObject& mesh) {
    MeshFingerprint fp;
    std::vector<bool> used(mesh.positions.size(), false);
    for (const auto& f : mesh.faces) {
        for (int i = 0; i < f.num_verts; i++) {
            int pi = f.corners[i].position_idx;
            if (pi >= 0 && pi < static_cast<int>(used.size())) used[pi] = true;
        }
    }
    MeshVec3 lo{ 1e9f, 1e9f, 1e9f }, hi{ -1e9f, -1e9f, -1e9f }, sum{ 0, 0, 0 };
    for (size_t i = 0; i < used.size(); i++) {
        if (!used[i]) continue;
        const auto& p = mesh.positions[i];
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
        sum.x += p.x; sum.y += p.y; sum.z += p.z;
        fp.vertex_count++;
    }
    if (fp.vertex_count > 0) {
        fp.center = { sum.x / fp.vertex_count, sum.y / fp.vertex_count, sum.z / fp.vertex_count };
        fp.extent = { hi.x - lo.x, hi.y - lo.y, hi.z - lo.z };
    }
    return fp;
}

MeshFingerprint ComputeTmdFingerprint(const tmd::TMD_Object& obj) {
    MeshFingerprint fp;
    if (obj.vertices.empty()) return fp;
    MeshVec3 lo{ 1e9f, 1e9f, 1e9f }, hi{ -1e9f, -1e9f, -1e9f }, sum{ 0, 0, 0 };
    for (const auto& v : obj.vertices) {
        // TMD is Y-down; flip to match GenericMeshObject's Y-up source space
        // (see RebuildTmdObject) so the two fingerprints are comparable.
        MeshVec3 p{ static_cast<float>(v.x), -static_cast<float>(v.y), static_cast<float>(v.z) };
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
        sum.x += p.x; sum.y += p.y; sum.z += p.z;
    }
    fp.vertex_count = static_cast<int>(obj.vertices.size());
    fp.center = { sum.x / fp.vertex_count, sum.y / fp.vertex_count, sum.z / fp.vertex_count };
    fp.extent = { hi.x - lo.x, hi.y - lo.y, hi.z - lo.z };
    return fp;
}

float FingerprintDistance(const MeshFingerprint& a, const MeshFingerprint& b) {
    float dcx = a.center.x - b.center.x, dcy = a.center.y - b.center.y, dcz = a.center.z - b.center.z;
    float dex = a.extent.x - b.extent.x, dey = a.extent.y - b.extent.y, dez = a.extent.z - b.extent.z;
    return std::sqrt(dcx * dcx + dcy * dcy + dcz * dcz) + std::sqrt(dex * dex + dey * dey + dez * dez);
}

} // namespace

int ParseTmdObjectIndex(const std::string& name) {
    static const std::string kPrefix = "Object";
    if (name.rfind(kPrefix, 0) != 0) return -1;
    size_t i = kPrefix.size();
    size_t start = i;
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) i++;
    if (i == start) return -1;
    return std::stoi(name.substr(start, i - start));
}

std::vector<bool> ResolveUnmatchedObjects(std::vector<GenericMeshObject>& meshes, const tmd::TMD_Model& model) {
    std::vector<bool> matched_by_geometry(meshes.size(), false);
    std::vector<bool> claimed(model.objects.size(), false);
    std::vector<size_t> unresolved;
    for (size_t i = 0; i < meshes.size(); i++) {
        int idx = meshes[i].object_index;
        if (idx >= 0 && idx < static_cast<int>(model.objects.size())) {
            claimed[idx] = true;
        } else {
            unresolved.push_back(i);
        }
    }
    if (unresolved.empty()) return matched_by_geometry;

    struct Candidate {
        size_t mesh_i;
        int object_index;
        float score;
    };
    std::vector<Candidate> candidates;
    for (size_t mi : unresolved) {
        MeshFingerprint mesh_fp = ComputeMeshFingerprint(meshes[mi]);
        if (mesh_fp.vertex_count == 0) continue;
        for (size_t oi = 0; oi < model.objects.size(); oi++) {
            if (claimed[oi]) continue;
            MeshFingerprint obj_fp = ComputeTmdFingerprint(model.objects[oi]);
            if (obj_fp.vertex_count == 0) continue;
            candidates.push_back({ mi, static_cast<int>(oi), FingerprintDistance(mesh_fp, obj_fp) });
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.score < b.score; });

    for (const auto& c : candidates) {
        if (matched_by_geometry[c.mesh_i] || claimed[c.object_index]) continue;
        meshes[c.mesh_i].object_index = c.object_index;
        matched_by_geometry[c.mesh_i] = true;
        claimed[c.object_index] = true;
    }
    return matched_by_geometry;
}

std::vector<std::vector<MeshCorner>> SplitFaceCorners(const std::vector<MeshCorner>& corners) {
    int n = static_cast<int>(corners.size());
    std::vector<std::vector<MeshCorner>> groups;
    if (n <= 4) {
        groups.push_back(corners);
        return groups;
    }

    // Fan-triangulate from corner 0 (n-2 triangles), then merge consecutive
    // pairs of those fan triangles back into quads sharing their diagonal,
    // leaving one triangle unmerged if the count is odd - see
    // tmd_mesh_import.h for the derivation.
    int num_fan_tris = n - 2;
    int fan_tri_index = 0;
    int v = 1;
    while (num_fan_tris - fan_tri_index >= 2) {
        groups.push_back({ corners[0], corners[v], corners[v + 1], corners[v + 2] });
        v += 2;
        fan_tri_index += 2;
    }
    if (num_fan_tris - fan_tri_index == 1) {
        groups.push_back({ corners[0], corners[v], corners[v + 1] });
    }
    return groups;
}

tmd::TMD_Object RebuildTmdObject(const GenericMeshObject& mesh, MeshImportStats& stats_out) {
    tmd::TMD_Object obj;
    std::unordered_map<int, int> vertex_remap; // source position index -> local TMD vertex index

    auto LocalVertex = [&](int position_idx) -> uint16_t {
        auto it = vertex_remap.find(position_idx);
        if (it != vertex_remap.end()) return static_cast<uint16_t>(it->second);
        int local = static_cast<int>(obj.vertices.size());
        vertex_remap[position_idx] = local;
        MeshVec3 p = (position_idx >= 0 && position_idx < static_cast<int>(mesh.positions.size()))
                         ? mesh.positions[position_idx]
                         : MeshVec3{ 0, 0, 0 };
        tmd::TMD_Vertex v;
        v.x = ClampRoundI16(p.x);
        v.y = ClampRoundI16(-p.y); // Y-up (source) -> Y-down (TMD)
        v.z = ClampRoundI16(p.z);
        obj.vertices.push_back(v);
        return static_cast<uint16_t>(local);
    };

    for (const GenericFace& face : mesh.faces) {
        tmd::TMD_Polygon poly;
        poly.num_verts = face.num_verts;
        poly.gouraud = false;
        poly.no_light = false;
        poly.double_sided = false;
        poly.semi_transparent = false;
        poly.color_per_vertex = false;
        poly.olen = 0; // fresh primitive - tmd_writer.cpp's fallback table computes it

        // Corners arrive in perimeter order; TMD stores a quad's corners in
        // triangle-strip order - see tmd::kQuadPerimeterOrder.
        int remapped_vert[4];
        int remapped_uv[4];
        bool all_have_uv = true;
        MeshVec3 normal_sum{ 0, 0, 0 };
        int normal_count = 0;

        for (int k = 0; k < face.num_verts; k++) {
            int c = (face.num_verts == 4) ? tmd::kQuadPerimeterOrder[k] : k;
            const MeshCorner& corner = face.corners[c];
            remapped_vert[k] = LocalVertex(corner.position_idx);
            remapped_uv[k] = corner.uv_idx;
            if (corner.uv_idx < 0) all_have_uv = false;
            if (corner.normal_idx >= 0 && corner.normal_idx < static_cast<int>(mesh.normals.size())) {
                MeshVec3 n = mesh.normals[corner.normal_idx];
                normal_sum = Add(normal_sum, { n.x, -n.y, n.z }); // undo Y-up -> TMD Y-down
                normal_count++;
            }
        }

        for (int k = 0; k < face.num_verts; k++) poly.vert_idx[k] = static_cast<uint16_t>(remapped_vert[k]);

        // Flat shading: one shared normal for the whole primitive, from
        // the face's own imported normal data (averaged if it had more
        // than one), or a computed geometric fallback if it had none.
        MeshVec3 normal;
        if (normal_count > 0) {
            normal = Normalize(Scale(normal_sum, 1.0f / static_cast<float>(normal_count)));
        } else {
            tmd::TMD_Vertex& v0 = obj.vertices[poly.vert_idx[0]];
            tmd::TMD_Vertex& v1 = obj.vertices[poly.vert_idx[1]];
            tmd::TMD_Vertex& v2 = obj.vertices[poly.vert_idx[2]];
            MeshVec3 p0{ static_cast<float>(v0.x), static_cast<float>(v0.y), static_cast<float>(v0.z) };
            MeshVec3 p1{ static_cast<float>(v1.x), static_cast<float>(v1.y), static_cast<float>(v1.z) };
            MeshVec3 p2{ static_cast<float>(v2.x), static_cast<float>(v2.y), static_cast<float>(v2.z) };
            normal = Normalize(Cross(Sub(p1, p0), Sub(p2, p0)));
        }
        tmd::TMD_Normal tn;
        tn.x = ClampRoundI16(normal.x * 4096.0f);
        tn.y = ClampRoundI16(normal.y * 4096.0f);
        tn.z = ClampRoundI16(normal.z * 4096.0f);
        obj.normals.push_back(tn);
        uint16_t norm_idx = static_cast<uint16_t>(obj.normals.size() - 1);
        for (int k = 0; k < face.num_verts; k++) poly.norm_idx[k] = norm_idx;

        if (all_have_uv) {
            poly.textured = true;
            poly.tsb = 0;
            poly.cba = 0;
            for (int k = 0; k < face.num_verts; k++) {
                MeshVec2 uv = mesh.uvs[remapped_uv[k]];
                poly.u[k] = ClampByteU8(uv.x * 255.0f);
                poly.v[k] = ClampByteU8((1.0f - uv.y) * 255.0f);
            }
        } else {
            poly.textured = false;
            uint8_t r, g, b;
            if (face.has_color) {
                r = ClampByteU8(face.color.x * 255.0f);
                g = ClampByteU8(face.color.y * 255.0f);
                b = ClampByteU8(face.color.z * 255.0f);
            } else {
                r = 200; g = 0; b = 200; // placeholder purple - obviously a stand-in, not a plausible real color
            }
            for (int k = 0; k < face.num_verts; k++) {
                poly.color[k][0] = r;
                poly.color[k][1] = g;
                poly.color[k][2] = b;
            }
        }

        obj.polygons.push_back(poly);
    }

    stats_out.face_count = static_cast<int>(mesh.faces.size());
    stats_out.vertex_count = static_cast<int>(obj.vertices.size());
    return obj;
}

} // namespace gfx
