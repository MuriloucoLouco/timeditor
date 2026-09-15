// Verifies gfx::Mat4::Inverse() against the two shapes the 3D viewport
// actually builds every frame: a perspective*view matrix (for unprojecting
// screen picks) and a translate*rotate*scale model matrix.
#include "gfx/math3d.h"
#include <cmath>
#include <cstdio>

using namespace gfx;

bool NearIdentity(const Mat4& m, float eps = 1e-3f) {
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float expected = (col == row) ? 1.0f : 0.0f;
            if (std::abs(m.m[col * 4 + row] - expected) > eps) return false;
        }
    }
    return true;
}

int main() {
    Mat4 view = Mat4::LookAt({ 50, 80, 200 }, { 0, 20, 0 }, { 0, 1, 0 });
    Mat4 proj = Mat4::Perspective(60.0f * 3.14159265f / 180.0f, 1280.0f / 720.0f, 1.0f, 1000000.0f);
    Mat4 view_proj = proj * view;
    Mat4 inv = view_proj.Inverse();
    Mat4 product = view_proj * inv;

    bool ok = NearIdentity(product);
    printf("view_proj * inverse == identity: %s\n", ok ? "YES" : "NO");
    if (!ok) {
        for (int i = 0; i < 16; i++) printf("%.4f ", product.m[i]);
        printf("\n");
    }

    Mat4 model = Mat4::Translate({ 10, 20, 30 }) * Mat4::RotateY(0.7f) * Mat4::Scale({ 2, 2, 2 });
    Mat4 model_inv = model.Inverse();
    Mat4 product2 = model * model_inv;
    bool ok2 = NearIdentity(product2);
    printf("model * inverse == identity: %s\n", ok2 ? "YES" : "NO");

    return (ok && ok2) ? 0 : 1;
}
