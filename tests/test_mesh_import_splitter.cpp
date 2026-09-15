// Verifies gfx::SplitFaceCorners (an n-gon fan-triangulated into 3/4-vertex
// TMD faces, since TMD only stores tris/quads): every group must be size 3
// or 4, and the pivot corner (index 0) must appear in every group produced
// from a fan triangulation around it.
#include "gfx/tmd_mesh_import.h"
#include <cstdio>
#include <vector>

using namespace gfx;

int main() {
    int failures = 0;
    for (int n = 3; n <= 8; n++) {
        std::vector<MeshCorner> corners;
        for (int i = 0; i < n; i++) corners.push_back({ i, -1, -1 });
        auto groups = SplitFaceCorners(corners);
        printf("n=%d -> %zu face(s): ", n, groups.size());
        for (auto& g : groups) {
            printf("[");
            for (auto& c : g) printf("%d ", c.position_idx);
            printf("](%zu) ", g.size());
        }
        printf("\n");

        bool ok = true;
        for (auto& g : groups) {
            if (g.size() != 3 && g.size() != 4) ok = false;
            bool has_pivot = false;
            for (auto& c : g) if (c.position_idx == 0) has_pivot = true;
            if (!has_pivot) ok = false;
        }
        printf("  valid=%s\n", ok ? "YES" : "NO");
        if (!ok) failures++;
    }

    printf(failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d TEST(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
