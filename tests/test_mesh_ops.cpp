// Verification of core/tmd_mesh_ops against hand-derived expected results
// on two synthetic meshes: a single flat quad, and a cube (8 verts, 6 quad
// faces, each of the 12 real cube edges appearing in exactly two of the six
// face loops - verified by hand in the comments below).
#include "core/tmd_format.h"
#include "core/tmd_mesh_ops.h"
#include <cmath>
#include <cstdio>

using namespace tmd;

namespace {
int failures = 0;
void Check(bool cond, const char* msg) {
    if (!cond) { printf("FAILED: %s\n", msg); failures++; }
    else printf("OK: %s\n", msg);
}

TMD_Object MakeQuad() {
    TMD_Object obj;
    AddVertex(obj, 0, 0, 0);   // 0
    AddVertex(obj, 10, 0, 0);  // 1
    AddVertex(obj, 10, 10, 0); // 2
    AddVertex(obj, 0, 10, 0);  // 3
    AddFace(obj, { 0, 1, 2, 3 });
    return obj;
}

// 8 corners of a +-10 cube, bit-indexed (bit0=x, bit1=y, bit2=z). Six quad
// faces, each a valid non-self-intersecting perimeter loop; hand-verified
// that all 12 real cube edges occur in exactly two of the six loops - i.e.
// this is a properly closed 2-manifold, good for interior-vs-boundary edge
// tests.
TMD_Object MakeCube() {
    TMD_Object obj;
    for (int i = 0; i < 8; i++) {
        float x = (i & 1) ? 10.0f : -10.0f;
        float y = (i & 2) ? 10.0f : -10.0f;
        float z = (i & 4) ? 10.0f : -10.0f;
        AddVertex(obj, x, y, z);
    }
    AddFace(obj, { 0, 1, 3, 2 }); // F0: z-
    AddFace(obj, { 4, 5, 7, 6 }); // F1: z+
    AddFace(obj, { 0, 1, 5, 4 }); // F2: y-
    AddFace(obj, { 2, 3, 7, 6 }); // F3: y+
    AddFace(obj, { 0, 2, 6, 4 }); // F4: x-
    AddFace(obj, { 1, 3, 7, 5 }); // F5: x+
    return obj;
}
} // namespace

int main() {
    // --- BuildEdgeList ---
    {
        TMD_Object q = MakeQuad();
        auto edges = BuildEdgeList(q);
        Check(edges.size() == 4, "quad has exactly 4 edges");
        bool all_boundary = true;
        for (auto& e : edges) if (e.primitive_indices.size() != 1) all_boundary = false;
        Check(all_boundary, "every quad edge is boundary (used by exactly 1 face)");
    }
    {
        TMD_Object cube = MakeCube();
        auto edges = BuildEdgeList(cube);
        Check(edges.size() == 12, "cube has exactly 12 edges");
        bool all_interior = true;
        for (auto& e : edges) if (e.primitive_indices.size() != 2) all_interior = false;
        Check(all_interior, "every cube edge is interior (used by exactly 2 faces, fully closed)");
    }

    // --- DeleteFaces ---
    {
        TMD_Object cube = MakeCube();
        DeleteFaces(cube, { 0 });
        Check(cube.polygons.size() == 5, "DeleteFaces: 5 faces remain after deleting 1 of 6");
        Check(cube.vertices.size() == 8, "DeleteFaces: vertices untouched");
    }

    // --- DeleteVertices (cascade) ---
    {
        TMD_Object cube = MakeCube();
        // Vertex 0 is used by F0={0,1,3,2}, F2={0,1,5,4}, F4={0,2,6,4} -> 3 faces.
        int cascaded = DeleteVertices(cube, { 0 });
        Check(cascaded == 3, "DeleteVertices: cascaded exactly the 3 faces touching vertex 0");
        Check(cube.polygons.size() == 3, "DeleteVertices: 3 faces remain");
        Check(cube.vertices.size() == 7, "DeleteVertices: 7 vertices remain (1 removed)");
        bool all_in_range = true;
        for (auto& p : cube.polygons) {
            for (int k = 0; k < p.num_verts; k++) {
                if (p.vert_idx[k] >= cube.vertices.size()) all_in_range = false;
            }
        }
        Check(all_in_range, "DeleteVertices: every surviving primitive's vert_idx is in range after compaction");
    }

    // --- MergeVertices (Average) ---
    {
        TMD_Object q = MakeQuad();
        int dropped = MergeVertices(q, { 0, 1 }, MergeTarget::Average);
        Check(dropped == 1, "MergeVertices(Average): the quad's only face becomes degenerate and is dropped");
        Check(q.polygons.empty(), "MergeVertices(Average): 0 faces remain");
        Check(q.vertices.size() == 4,
              "MergeVertices(Average): vertex table size unchanged (merged verts kept, just unreferenced)");
        Check(q.vertices[0].x == 5 && q.vertices[0].y == 0 && q.vertices[0].z == 0,
              "MergeVertices(Average): survivor moved to the midpoint of (0,0,0) and (10,0,0)");
    }

    // --- MergeVertices (Last) ---
    {
        TMD_Object q = MakeQuad();
        int dropped = MergeVertices(q, { 0, 2 }, MergeTarget::Last);
        Check(dropped == 1, "MergeVertices(Last): the quad's only face becomes degenerate and is dropped");
        Check(q.vertices[0].x == 10 && q.vertices[0].y == 10 && q.vertices[0].z == 0,
              "MergeVertices(Last): survivor took vertex 2's exact original position");
    }

    // --- AddFace validation ---
    {
        TMD_Object q = MakeQuad();
        Check(AddFace(q, { 0, 1 }) == -1, "AddFace: rejects a 2-vertex list");
        Check(AddFace(q, { 0, 1, 2, 3, 0 }) == -1, "AddFace: rejects a 5-vertex list");
        Check(AddFace(q, { 0, 1, 99 }) == -1, "AddFace: rejects an out-of-range vertex index");
    }

    // --- AddFace's computed normal direction ---
    {
        TMD_Object q = MakeQuad();
        // Perimeter corners 0,1,2 = (0,0,0),(10,0,0),(10,10,0) ->
        // cross((10,0,0),(10,10,0)) = (0,0,100) -> normalized*4096 = (0,0,4096).
        Check(q.normals.size() == 1, "AddFace: pushed exactly one normal");
        Check(std::abs(q.normals[0].x) <= 1 && std::abs(q.normals[0].y) <= 1 && q.normals[0].z > 4000,
              "AddFace: computed normal points along +Z as expected from the winding");
    }

    // --- ExtrudeFaces: whole single quad (every edge is its own boundary) ---
    {
        TMD_Object q = MakeQuad();
        ExtrudeResult r = ExtrudeFaces(q, { 0 });
        Check(r.new_vertex_indices.size() == 4, "ExtrudeFaces(quad): 4 new vertices (one per corner)");
        Check(r.new_wall_primitive_indices.size() == 4, "ExtrudeFaces(quad): 4 new wall quads (every edge is boundary)");
        Check(q.vertices.size() == 8, "ExtrudeFaces(quad): vertex table grew by 4");
        Check(q.polygons.size() == 5, "ExtrudeFaces(quad): 1 original (re-pointed cap) + 4 walls");
        bool cap_repointed = true;
        for (int k = 0; k < q.polygons[0].num_verts; k++) if (q.polygons[0].vert_idx[k] < 4) cap_repointed = false;
        Check(cap_repointed, "ExtrudeFaces(quad): the original primitive index now references only the new (>=4) vertices");
    }

    // --- ExtrudeFaces: two adjacent cube faces sharing one edge ---
    {
        TMD_Object cube = MakeCube();
        // F0={0,1,3,2} and F2={0,1,5,4} share edge (0,1) - the only interior
        // edge of this 2-face selection; the other 6 edges among them are
        // each used by just one of the two, so 6 walls are expected, not 8.
        ExtrudeResult r = ExtrudeFaces(cube, { 0, 2 });
        Check(r.new_vertex_indices.size() == 6, "ExtrudeFaces(2 adjacent cube faces): 6 distinct old vertices duplicated");
        Check(r.new_wall_primitive_indices.size() == 6, "ExtrudeFaces(2 adjacent cube faces): 6 walls (shared edge gets none)");
    }

    // --- ExtrudeEdge ---
    {
        TMD_Object q = MakeQuad();
        ExtrudeResult r = ExtrudeEdge(q, 0, 1);
        Check(r.new_vertex_indices.size() == 2, "ExtrudeEdge: 2 new vertices");
        Check(r.new_wall_primitive_indices.size() == 1, "ExtrudeEdge: exactly 1 new wall quad");
        Check(q.vertices.size() == 6, "ExtrudeEdge: vertex table grew by 2");
        Check(q.polygons.size() == 2, "ExtrudeEdge: original face untouched, 1 new wall added");
    }

    // --- RemoveUnusedVerticesAndNormals ---
    {
        TMD_Object q = MakeQuad();
        MergeVertices(q, { 0, 1 }, MergeTarget::Average); // drops the only face -> everything becomes unused
        CleanupResult r = RemoveUnusedVerticesAndNormals(q);
        Check(r.vertices_removed == 4, "Cleanup: all 4 now-unreferenced vertices removed");
        Check(r.normals_removed == 1, "Cleanup: the 1 now-unreferenced normal removed");
        Check(q.vertices.empty() && q.normals.empty(), "Cleanup: both tables empty afterward");
    }

    // --- FlipNormal doesn't corrupt a sibling sharing the same normal index ---
    {
        TMD_Object q = MakeQuad(); // 1 face (index 0), 1 normal (index 0), all 4 corners -> norm_idx 0
        AddFace(q, { 3, 2, 1, 0 }); // a second face, deliberately sharing corner order variety
        int shared_normal = 0;
        for (int k = 0; k < 4; k++) q.polygons[1].norm_idx[k] = shared_normal; // force it to alias face 0's normal
        TMD_Normal original = q.normals[shared_normal];

        FlipNormal(q, 0);

        bool face0_moved_off_shared = true;
        for (int k = 0; k < 4; k++) if (q.polygons[0].norm_idx[k] == shared_normal) face0_moved_off_shared = false;
        Check(face0_moved_off_shared, "FlipNormal: the flipped face now points at a fresh entry, not the old shared one");

        bool face1_still_shared = true;
        for (int k = 0; k < 4; k++) if (q.polygons[1].norm_idx[k] != shared_normal) face1_still_shared = false;
        Check(face1_still_shared, "FlipNormal: the sibling face's norm_idx is untouched");

        Check(q.normals[shared_normal].x == original.x && q.normals[shared_normal].y == original.y &&
                  q.normals[shared_normal].z == original.z,
              "FlipNormal: the original shared normal's VALUE is untouched");

        uint16_t new_idx = q.polygons[0].norm_idx[0];
        Check(q.normals[new_idx].x == -original.x && q.normals[new_idx].y == -original.y &&
                  q.normals[new_idx].z == -original.z,
              "FlipNormal: the new entry is the exact negation of the original");
    }

    // --- RecalculateNormal resets from current geometry ---
    {
        TMD_Object q = MakeQuad();
        int old_normal_count = static_cast<int>(q.normals.size());
        RecalculateNormal(q, 0);
        Check(static_cast<int>(q.normals.size()) == old_normal_count + 1, "RecalculateNormal: pushes one fresh normal");
        uint16_t idx = q.polygons[0].norm_idx[0];
        bool all_same = true;
        for (int k = 0; k < q.polygons[0].num_verts; k++) if (q.polygons[0].norm_idx[k] != idx) all_same = false;
        Check(all_same, "RecalculateNormal: every corner now points at the same fresh normal");
        Check(q.normals[idx].z > 4000, "RecalculateNormal: recomputed normal still points along +Z for this unmoved quad");
    }

    printf(failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d TEST(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
