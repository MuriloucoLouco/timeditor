// Verification for the "don't give up on a mismatched object name"
// fallback: ParseTmdObjectIndex's prefix-anchored parsing (fixing the
// Blender ".001" duplicate-suffix misparse) and ResolveUnmatchedObjects's
// shape-based greedy matching, compiled directly against the real
// gfx/tmd_mesh_import.cpp/core/tmd_format.h.
#include "gfx/tmd_mesh_import.h"
#include <algorithm>
#include <cstdio>

using namespace gfx;

int main() {
    int failures = 0;

    // --- ParseTmdObjectIndex ---
    struct Case { const char* name; int expected; };
    Case cases[] = {
        { "Object0", 0 },
        { "Object42", 42 },
        { "Object42.001", 42 },   // Blender duplicate-name suffix - must NOT parse as 1
        { "Object5.002", 5 },
        { "Cube", -1 },
        { "", -1 },
        { "MyObject3", -1 },      // doesn't start with "Object"
        { "Object", -1 },         // no digits at all
    };
    for (auto& c : cases) {
        int got = ParseTmdObjectIndex(c.name);
        if (got != c.expected) {
            printf("ParseTmdObjectIndex(\"%s\") FAILED: got %d, expected %d\n", c.name, got, c.expected);
            failures++;
        } else {
            printf("ParseTmdObjectIndex(\"%s\") -> %d OK\n", c.name, got);
        }
    }

    // --- ResolveUnmatchedObjects ---
    auto MakeTmdObject = [](float cx, float cy, float cz, float size, int n) {
        tmd::TMD_Object obj;
        for (int i = 0; i < n; i++) {
            tmd::TMD_Vertex v;
            float t = static_cast<float>(i) / std::max(1, n - 1);
            v.x = static_cast<int16_t>(cx + (t - 0.5f) * size);
            v.y = static_cast<int16_t>(-(cy + (t - 0.5f) * size)); // stored Y-down
            v.z = static_cast<int16_t>(cz + (t - 0.5f) * size);
            obj.vertices.push_back(v);
        }
        return obj;
    };
    auto MakeGenericMesh = [](float cx, float cy, float cz, float size, int n, int object_index) {
        GenericMeshObject mesh;
        mesh.object_index = object_index;
        for (int i = 0; i < n; i++) {
            float t = static_cast<float>(i) / std::max(1, n - 1);
            mesh.positions.push_back({ cx + (t - 0.5f) * size, cy + (t - 0.5f) * size, cz + (t - 0.5f) * size });
        }
        GenericFace f;
        f.num_verts = 3;
        f.corners[0] = { 0, -1, -1 };
        f.corners[1] = { std::min(1, n - 1), -1, -1 };
        f.corners[2] = { std::min(2, n - 1), -1, -1 };
        mesh.faces.push_back(f);
        return mesh;
    };

    // Test A: single-object model, imported group has an unparseable name
    // -> the only unclaimed object, must resolve to it.
    {
        tmd::TMD_Model model;
        model.objects.push_back(MakeTmdObject(0, 0, 0, 100, 20));
        std::vector<GenericMeshObject> meshes;
        meshes.push_back(MakeGenericMesh(0, 0, 0, 100, 20, -1));
        auto matched = ResolveUnmatchedObjects(meshes, model);
        if (!matched[0] || meshes[0].object_index != 0) {
            printf("TestA FAILED: matched=%d object_index=%d\n", static_cast<int>(matched[0]), meshes[0].object_index);
            failures++;
        } else printf("TestA OK (single-object fallback)\n");
    }

    // Test B: two objects far apart, two renamed groups matching their
    // shape/position closely - must resolve to the correct one each,
    // not just in file order.
    {
        tmd::TMD_Model model;
        model.objects.push_back(MakeTmdObject(0, 0, 0, 50, 10));      // object 0: small, near origin
        model.objects.push_back(MakeTmdObject(5000, 5000, 5000, 800, 10)); // object 1: huge, far away
        std::vector<GenericMeshObject> meshes;
        // Deliberately list the "far away" one first in the file, to prove
        // it's not just assigned by encounter order.
        meshes.push_back(MakeGenericMesh(5000, 5000, 5000, 800, 10, -1));
        meshes.push_back(MakeGenericMesh(0, 0, 0, 50, 10, -1));
        auto matched = ResolveUnmatchedObjects(meshes, model);
        bool ok = matched[0] && matched[1] && meshes[0].object_index == 1 && meshes[1].object_index == 0;
        if (!ok) {
            printf("TestB FAILED: [0]->%d (matched=%d), [1]->%d (matched=%d)\n", meshes[0].object_index,
                   static_cast<int>(matched[0]), meshes[1].object_index, static_cast<int>(matched[1]));
            failures++;
        } else printf("TestB OK (matched by shape, not file order)\n");
    }

    // Test C: a name that DOES parse correctly must be left untouched
    // (never overridden by geometry, even if a "closer" shape exists
    // elsewhere) - name match takes priority whenever it's valid.
    {
        tmd::TMD_Model model;
        model.objects.push_back(MakeTmdObject(0, 0, 0, 50, 10));
        model.objects.push_back(MakeTmdObject(0, 0, 0, 50, 10)); // identical shape, different index
        std::vector<GenericMeshObject> meshes;
        meshes.push_back(MakeGenericMesh(0, 0, 0, 50, 10, 1)); // explicitly named "Object1"
        auto matched = ResolveUnmatchedObjects(meshes, model);
        if (matched[0] || meshes[0].object_index != 1) {
            printf("TestC FAILED: matched=%d object_index=%d\n", static_cast<int>(matched[0]), meshes[0].object_index);
            failures++;
        } else printf("TestC OK (valid name never overridden)\n");
    }

    // Test D: more unresolved groups than unclaimed objects - leftover
    // group(s) must stay unresolved (safe skip) rather than double-claim.
    {
        tmd::TMD_Model model;
        model.objects.push_back(MakeTmdObject(0, 0, 0, 50, 10));
        std::vector<GenericMeshObject> meshes;
        meshes.push_back(MakeGenericMesh(0, 0, 0, 50, 10, -1));
        meshes.push_back(MakeGenericMesh(9999, 9999, 9999, 50, 10, -1));
        auto matched = ResolveUnmatchedObjects(meshes, model);
        int resolved_count = (matched[0] ? 1 : 0) + (matched[1] ? 1 : 0);
        if (resolved_count != 1) {
            printf("TestD FAILED: resolved_count=%d (expected exactly 1)\n", resolved_count);
            failures++;
        } else printf("TestD OK (leftover group stays unresolved, no double-claim)\n");
    }

    printf(failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d TEST(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
