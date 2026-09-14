#include "tmd_mesh_ops.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace tmd {

namespace {

int16_t ClampRoundI16(float v) { return static_cast<int16_t>(std::clamp(std::lround(v), -32768L, 32767L)); }
int16_t NegateClampedI16(int16_t v) {
    long r = -static_cast<long>(v);
    return static_cast<int16_t>(std::clamp(r, -32768L, 32767L));
}

struct V3 {
    float x = 0, y = 0, z = 0;
};
V3 Sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3 Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
float Length(V3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
V3 Normalize(V3 v) {
    float len = Length(v);
    return len > 1e-6f ? V3{ v.x / len, v.y / len, v.z / len } : V3{ 0, 0, 1 };
}
V3 VertexPos(const TMD_Object& obj, int vi) {
    const TMD_Vertex& v = obj.vertices[vi];
    return { static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z) };
}

// GTE fixed-point convention already used elsewhere in this codebase
// (see tmd_raw_editor.cpp's normal-editing tooltip): 4096 = 1.0 along an axis.
constexpr float kNormalScale = 4096.0f;

// Converts a quad's 4 corners between TMD's native strip storage order and
// perimeter (outline) order - self-inverse, so the exact same formula
// converts in either direction (see tmd::kQuadPerimeterOrder's own
// comment). Kept as the one place this table is ever applied in this file.
void ConvertQuadOrder(const uint16_t in[4], uint16_t out[4]) {
    for (int k = 0; k < 4; k++) out[k] = in[kQuadPerimeterOrder[k]];
}

// A primitive's corners in perimeter (outline) order - direct copy for a
// tri (no strip/perimeter distinction there), permuted for a quad.
void PerimeterVertIndices(const TMD_Polygon& p, uint16_t out[4]) {
    if (p.num_verts == 4) {
        ConvertQuadOrder(p.vert_idx, out);
    } else {
        for (int k = 0; k < p.num_verts; k++) out[k] = p.vert_idx[k];
    }
}

// Computes a fresh flat normal from three positions' winding
// ((p1-p0) x (p2-p0)), pushes it onto obj.normals, returns its index.
// Degenerate (collinear/zero-area) input falls back to +Z rather than
// pushing a zero-length normal.
int PushComputedNormal(TMD_Object& obj, V3 p0, V3 p1, V3 p2) {
    V3 n = Normalize(Cross(Sub(p1, p0), Sub(p2, p0)));
    TMD_Normal tn;
    tn.x = ClampRoundI16(n.x * kNormalScale);
    tn.y = ClampRoundI16(n.y * kNormalScale);
    tn.z = ClampRoundI16(n.z * kNormalScale);
    obj.normals.push_back(tn);
    return static_cast<int>(obj.normals.size()) - 1;
}

} // namespace

std::vector<EdgeUsage> BuildEdgeList(const TMD_Object& obj, const std::vector<int>& primitive_indices) {
    std::vector<int> indices = primitive_indices;
    if (indices.empty()) {
        indices.resize(obj.polygons.size());
        for (size_t i = 0; i < indices.size(); i++) indices[i] = static_cast<int>(i);
    }

    std::map<Edge, std::vector<int>> usage;
    for (int pi : indices) {
        if (pi < 0 || pi >= static_cast<int>(obj.polygons.size())) continue;
        const TMD_Polygon& p = obj.polygons[pi];
        uint16_t perim[4];
        PerimeterVertIndices(p, perim);
        for (int k = 0; k < p.num_verts; k++) {
            uint16_t a = perim[k], b = perim[(k + 1) % p.num_verts];
            usage[MakeEdge(a, b)].push_back(pi);
        }
    }

    std::vector<EdgeUsage> result;
    result.reserve(usage.size());
    for (auto& [edge, prims] : usage) result.push_back({ edge, std::move(prims) });
    return result;
}

void DeleteFaces(TMD_Object& obj, const std::vector<int>& primitive_indices) {
    std::vector<bool> remove(obj.polygons.size(), false);
    for (int i : primitive_indices) {
        if (i >= 0 && i < static_cast<int>(remove.size())) remove[i] = true;
    }
    std::vector<TMD_Polygon> kept;
    kept.reserve(obj.polygons.size());
    for (size_t i = 0; i < obj.polygons.size(); i++) {
        if (!remove[i]) kept.push_back(obj.polygons[i]);
    }
    obj.polygons = std::move(kept);
}

int DeleteVertices(TMD_Object& obj, const std::vector<int>& vertex_indices) {
    std::vector<bool> remove_vert(obj.vertices.size(), false);
    for (int i : vertex_indices) {
        if (i >= 0 && i < static_cast<int>(remove_vert.size())) remove_vert[i] = true;
    }

    std::vector<bool> remove_prim(obj.polygons.size(), false);
    int cascaded = 0;
    for (size_t pi = 0; pi < obj.polygons.size(); pi++) {
        const TMD_Polygon& p = obj.polygons[pi];
        for (int k = 0; k < p.num_verts; k++) {
            if (p.vert_idx[k] < remove_vert.size() && remove_vert[p.vert_idx[k]]) {
                remove_prim[pi] = true;
                cascaded++;
                break;
            }
        }
    }

    std::vector<int> remap(obj.vertices.size(), -1);
    std::vector<TMD_Vertex> new_verts;
    new_verts.reserve(obj.vertices.size());
    for (size_t vi = 0; vi < obj.vertices.size(); vi++) {
        if (remove_vert[vi]) continue;
        remap[vi] = static_cast<int>(new_verts.size());
        new_verts.push_back(obj.vertices[vi]);
    }
    obj.vertices = std::move(new_verts);

    std::vector<TMD_Polygon> new_polys;
    new_polys.reserve(obj.polygons.size());
    for (size_t pi = 0; pi < obj.polygons.size(); pi++) {
        if (remove_prim[pi]) continue;
        TMD_Polygon p = obj.polygons[pi];
        for (int k = 0; k < p.num_verts; k++) p.vert_idx[k] = static_cast<uint16_t>(remap[p.vert_idx[k]]);
        new_polys.push_back(p);
    }
    obj.polygons = std::move(new_polys);

    return cascaded;
}

int MergeVertices(TMD_Object& obj, const std::vector<int>& vertex_indices, MergeTarget target) {
    if (vertex_indices.size() < 2) return 0;
    for (int vi : vertex_indices) {
        if (vi < 0 || vi >= static_cast<int>(obj.vertices.size())) return 0;
    }

    int survivor = vertex_indices[0];
    if (target == MergeTarget::Average) {
        float sx = 0, sy = 0, sz = 0;
        for (int vi : vertex_indices) {
            sx += obj.vertices[vi].x;
            sy += obj.vertices[vi].y;
            sz += obj.vertices[vi].z;
        }
        float n = static_cast<float>(vertex_indices.size());
        obj.vertices[survivor].x = ClampRoundI16(sx / n);
        obj.vertices[survivor].y = ClampRoundI16(sy / n);
        obj.vertices[survivor].z = ClampRoundI16(sz / n);
    } else {
        obj.vertices[survivor] = obj.vertices[vertex_indices.back()];
    }

    std::vector<int> remap(obj.vertices.size());
    for (size_t i = 0; i < remap.size(); i++) remap[i] = static_cast<int>(i);
    for (size_t i = 1; i < vertex_indices.size(); i++) remap[vertex_indices[i]] = survivor;

    for (auto& p : obj.polygons) {
        for (int k = 0; k < p.num_verts; k++) p.vert_idx[k] = static_cast<uint16_t>(remap[p.vert_idx[k]]);
    }

    std::vector<bool> drop(obj.polygons.size(), false);
    int dropped = 0;
    for (size_t pi = 0; pi < obj.polygons.size(); pi++) {
        const TMD_Polygon& p = obj.polygons[pi];
        for (int a = 0; a < p.num_verts && !drop[pi]; a++) {
            for (int b = a + 1; b < p.num_verts; b++) {
                if (p.vert_idx[a] == p.vert_idx[b]) {
                    drop[pi] = true;
                    dropped++;
                    break;
                }
            }
        }
    }
    std::vector<TMD_Polygon> kept;
    kept.reserve(obj.polygons.size());
    for (size_t pi = 0; pi < obj.polygons.size(); pi++) {
        if (!drop[pi]) kept.push_back(obj.polygons[pi]);
    }
    obj.polygons = std::move(kept);

    return dropped;
}

int AddVertex(TMD_Object& obj, float x, float y, float z) {
    TMD_Vertex v;
    v.x = ClampRoundI16(x);
    v.y = ClampRoundI16(y);
    v.z = ClampRoundI16(z);
    obj.vertices.push_back(v);
    return static_cast<int>(obj.vertices.size()) - 1;
}

int AddFace(TMD_Object& obj, const std::vector<int>& vertex_indices_perimeter_order) {
    int n = static_cast<int>(vertex_indices_perimeter_order.size());
    if (n != 3 && n != 4) return -1;
    for (int vi : vertex_indices_perimeter_order) {
        if (vi < 0 || vi >= static_cast<int>(obj.vertices.size())) return -1;
    }

    TMD_Polygon p;
    p.num_verts = n;
    p.textured = false;
    p.gouraud = false;
    p.no_light = false;
    p.double_sided = false;
    p.semi_transparent = false;
    p.color_per_vertex = false;
    p.olen = 0;
    p.cba = 0;
    p.tsb = 0;
    for (int k = 0; k < 4; k++) {
        // Placeholder purple - the same "brand new, unassigned" convention
        // gfx::RebuildTmdObject already uses for a freshly built face.
        p.color[k][0] = 200;
        p.color[k][1] = 0;
        p.color[k][2] = 200;
        p.u[k] = 0;
        p.v[k] = 0;
    }

    if (n == 4) {
        uint16_t perim[4] = {
            static_cast<uint16_t>(vertex_indices_perimeter_order[0]),
            static_cast<uint16_t>(vertex_indices_perimeter_order[1]),
            static_cast<uint16_t>(vertex_indices_perimeter_order[2]),
            static_cast<uint16_t>(vertex_indices_perimeter_order[3]),
        };
        uint16_t strip[4];
        ConvertQuadOrder(perim, strip);
        for (int k = 0; k < 4; k++) p.vert_idx[k] = strip[k];
    } else {
        for (int k = 0; k < 3; k++) p.vert_idx[k] = static_cast<uint16_t>(vertex_indices_perimeter_order[k]);
    }

    V3 p0 = VertexPos(obj, vertex_indices_perimeter_order[0]);
    V3 p1 = VertexPos(obj, vertex_indices_perimeter_order[1]);
    V3 p2 = VertexPos(obj, vertex_indices_perimeter_order[2]);
    int normal_idx = PushComputedNormal(obj, p0, p1, p2);
    for (int k = 0; k < n; k++) p.norm_idx[k] = static_cast<uint16_t>(normal_idx);

    obj.polygons.push_back(p);
    return static_cast<int>(obj.polygons.size()) - 1;
}

DuplicateResult DuplicateSelection(TMD_Object& obj, const std::vector<int>& vertex_indices,
                                    const std::vector<int>& primitive_indices) {
    DuplicateResult result;

    std::map<int, int> vmap;
    for (int vi : vertex_indices) {
        if (vi < 0 || vi >= static_cast<int>(obj.vertices.size())) {
            result.new_vertex_indices.push_back(-1);
            continue;
        }
        obj.vertices.push_back(obj.vertices[vi]);
        int new_idx = static_cast<int>(obj.vertices.size()) - 1;
        vmap[vi] = new_idx;
        result.new_vertex_indices.push_back(new_idx);
    }

    for (int pi : primitive_indices) {
        if (pi < 0 || pi >= static_cast<int>(obj.polygons.size())) {
            result.new_primitive_indices.push_back(-1);
            continue;
        }
        TMD_Polygon p = obj.polygons[pi];
        bool ok = true;
        for (int k = 0; k < p.num_verts && ok; k++) {
            auto it = vmap.find(p.vert_idx[k]);
            if (it == vmap.end()) {
                ok = false;
                break;
            }
            p.vert_idx[k] = static_cast<uint16_t>(it->second);
        }
        if (!ok) {
            result.new_primitive_indices.push_back(-1);
            continue;
        }
        obj.polygons.push_back(p);
        result.new_primitive_indices.push_back(static_cast<int>(obj.polygons.size()) - 1);
    }

    return result;
}

ExtrudeResult ExtrudeFaces(TMD_Object& obj, const std::vector<int>& primitive_indices) {
    ExtrudeResult result;

    std::vector<int> old_verts;
    std::map<int, int> seen;
    for (int pi : primitive_indices) {
        if (pi < 0 || pi >= static_cast<int>(obj.polygons.size())) continue;
        const TMD_Polygon& p = obj.polygons[pi];
        for (int k = 0; k < p.num_verts; k++) {
            int vi = p.vert_idx[k];
            if (seen.emplace(vi, 0).second) old_verts.push_back(vi);
        }
    }
    if (old_verts.empty()) return result;

    // Directed boundary edges, captured from the topology BEFORE any
    // re-pointing below - an edge used by exactly one of the given
    // primitives gets a wall; shared by two, it's interior to the
    // extruded region and gets none.
    std::map<Edge, int> edge_count;
    std::map<Edge, std::pair<uint16_t, uint16_t>> edge_dir;
    for (int pi : primitive_indices) {
        if (pi < 0 || pi >= static_cast<int>(obj.polygons.size())) continue;
        const TMD_Polygon& p = obj.polygons[pi];
        uint16_t perim[4];
        PerimeterVertIndices(p, perim);
        for (int k = 0; k < p.num_verts; k++) {
            uint16_t from = perim[k], to = perim[(k + 1) % p.num_verts];
            Edge e = MakeEdge(from, to);
            edge_count[e]++;
            edge_dir[e] = { from, to };
        }
    }

    std::map<int, int> old_to_new;
    for (int ov : old_verts) {
        obj.vertices.push_back(obj.vertices[ov]);
        int nv = static_cast<int>(obj.vertices.size()) - 1;
        old_to_new[ov] = nv;
        result.new_vertex_indices.push_back(nv);
    }

    // Re-point the original primitives to the new, duplicated vertices -
    // they become the moved "cap", keeping their own indices valid.
    for (int pi : primitive_indices) {
        if (pi < 0 || pi >= static_cast<int>(obj.polygons.size())) continue;
        TMD_Polygon& p = obj.polygons[pi];
        for (int k = 0; k < p.num_verts; k++) p.vert_idx[k] = static_cast<uint16_t>(old_to_new[p.vert_idx[k]]);
    }

    for (const auto& [edge, count] : edge_count) {
        if (count != 1) continue;
        auto [from, to] = edge_dir[edge];
        int new_from = old_to_new[from], new_to = old_to_new[to];
        int wall = AddFace(obj, { static_cast<int>(from), static_cast<int>(to), new_to, new_from });
        if (wall >= 0) result.new_wall_primitive_indices.push_back(wall);
    }

    return result;
}

ExtrudeResult ExtrudeEdge(TMD_Object& obj, int vertex_a, int vertex_b) {
    ExtrudeResult result;
    if (vertex_a < 0 || vertex_a >= static_cast<int>(obj.vertices.size())) return result;
    if (vertex_b < 0 || vertex_b >= static_cast<int>(obj.vertices.size())) return result;

    obj.vertices.push_back(obj.vertices[vertex_a]);
    int new_a = static_cast<int>(obj.vertices.size()) - 1;
    obj.vertices.push_back(obj.vertices[vertex_b]);
    int new_b = static_cast<int>(obj.vertices.size()) - 1;
    result.new_vertex_indices = { new_a, new_b };

    int wall = AddFace(obj, { vertex_a, vertex_b, new_b, new_a });
    if (wall >= 0) result.new_wall_primitive_indices.push_back(wall);
    return result;
}

void FlipNormal(TMD_Object& obj, int primitive_index) {
    if (primitive_index < 0 || primitive_index >= static_cast<int>(obj.polygons.size())) return;
    TMD_Polygon& p = obj.polygons[primitive_index];

    std::vector<uint16_t> distinct;
    for (int k = 0; k < p.num_verts; k++) {
        uint16_t ni = p.norm_idx[k];
        if (ni == TMD_Polygon::kNoNormal) continue;
        if (std::find(distinct.begin(), distinct.end(), ni) == distinct.end()) distinct.push_back(ni);
    }

    std::vector<std::pair<uint16_t, uint16_t>> remap;
    for (uint16_t ni : distinct) {
        if (ni >= obj.normals.size()) continue;
        TMD_Normal flipped;
        flipped.x = NegateClampedI16(obj.normals[ni].x);
        flipped.y = NegateClampedI16(obj.normals[ni].y);
        flipped.z = NegateClampedI16(obj.normals[ni].z);
        obj.normals.push_back(flipped);
        remap.push_back({ ni, static_cast<uint16_t>(obj.normals.size() - 1) });
    }

    for (int k = 0; k < p.num_verts; k++) {
        for (const auto& [old_idx, new_idx] : remap) {
            if (p.norm_idx[k] == old_idx) {
                p.norm_idx[k] = new_idx;
                break;
            }
        }
    }
}

void RecalculateNormal(TMD_Object& obj, int primitive_index) {
    if (primitive_index < 0 || primitive_index >= static_cast<int>(obj.polygons.size())) return;
    TMD_Polygon& p = obj.polygons[primitive_index];

    uint16_t perim[4];
    PerimeterVertIndices(p, perim);
    V3 p0 = VertexPos(obj, perim[0]);
    V3 p1 = VertexPos(obj, perim[1]);
    V3 p2 = VertexPos(obj, perim[2]);
    int normal_idx = PushComputedNormal(obj, p0, p1, p2);
    for (int k = 0; k < p.num_verts; k++) p.norm_idx[k] = static_cast<uint16_t>(normal_idx);
}

CleanupResult RemoveUnusedVerticesAndNormals(TMD_Object& obj) {
    CleanupResult result;

    std::vector<bool> vert_used(obj.vertices.size(), false);
    std::vector<bool> norm_used(obj.normals.size(), false);
    for (const auto& p : obj.polygons) {
        for (int k = 0; k < p.num_verts; k++) {
            if (p.vert_idx[k] < vert_used.size()) vert_used[p.vert_idx[k]] = true;
            uint16_t ni = p.norm_idx[k];
            if (ni != TMD_Polygon::kNoNormal && ni < norm_used.size()) norm_used[ni] = true;
        }
    }

    std::vector<int> vremap(obj.vertices.size(), -1);
    std::vector<TMD_Vertex> new_verts;
    for (size_t i = 0; i < obj.vertices.size(); i++) {
        if (!vert_used[i]) {
            result.vertices_removed++;
            continue;
        }
        vremap[i] = static_cast<int>(new_verts.size());
        new_verts.push_back(obj.vertices[i]);
    }

    std::vector<int> nremap(obj.normals.size(), -1);
    std::vector<TMD_Normal> new_norms;
    for (size_t i = 0; i < obj.normals.size(); i++) {
        if (!norm_used[i]) {
            result.normals_removed++;
            continue;
        }
        nremap[i] = static_cast<int>(new_norms.size());
        new_norms.push_back(obj.normals[i]);
    }

    for (auto& p : obj.polygons) {
        for (int k = 0; k < p.num_verts; k++) {
            p.vert_idx[k] = static_cast<uint16_t>(vremap[p.vert_idx[k]]);
            uint16_t ni = p.norm_idx[k];
            if (ni != TMD_Polygon::kNoNormal && ni < nremap.size()) p.norm_idx[k] = static_cast<uint16_t>(nremap[ni]);
        }
    }

    obj.vertices = std::move(new_verts);
    obj.normals = std::move(new_norms);
    return result;
}

} // namespace tmd
