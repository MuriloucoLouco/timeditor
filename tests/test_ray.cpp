// Verifies the gfx/ray.h helpers used by the Model Editor's vertex/edge/
// box-select picking: RayFromNDC+RayHitsTriangle (project a triangle's
// centroid to screen, unproject that exact screen position back into a
// ray, confirm it hits; confirm an off-target ray misses), ProjectPoint
// (the literal inverse operation), and DistancePointToSegment2D.
#include "gfx/ray.h"
#include <cmath>
#include <cstdio>

using namespace gfx;

int main() {
    int failures = 0;

    Vec3 eye{ 100, 150, 300 };
    Vec3 target{ 0, 20, 0 };
    Mat4 view = Mat4::LookAt(eye, target, { 0, 1, 0 });
    float vw = 1280, vh = 720;
    Mat4 proj = Mat4::Perspective(60.0f * 3.14159265f / 180.0f, vw / vh, 1.0f, 1000000.0f);
    Mat4 view_proj = proj * view;

    Vec3 v0{ -50, 0, 0 }, v1{ 50, 0, 0 }, v2{ 0, 80, 0 };
    Vec3 centroid = (v0 + v1 + v2) * (1.0f / 3.0f);

    // --- ProjectPoint + RayFromNDC round trip ---
    Vec3 screen;
    bool proj_ok = ProjectPoint(view_proj, centroid, vw, vh, screen);
    if (!proj_ok) { printf("Test1 FAILED: ProjectPoint returned false for a point in front of the camera\n"); failures++; }
    else printf("Test1 OK: projected centroid to screen (%.1f, %.1f)\n", screen.x, screen.y);

    float ndc_x = (screen.x / vw) * 2.0f - 1.0f;
    float ndc_y = 1.0f - (screen.y / vh) * 2.0f;
    Mat4 inv_vp = view_proj.Inverse();
    Ray ray = RayFromNDC(inv_vp, ndc_x, ndc_y);

    float t;
    bool hit = RayHitsTriangle(ray.origin, ray.dir, v0, v1, v2, t);
    if (!hit) { printf("Test2 FAILED: ray through the projected centroid did not hit the triangle\n"); failures++; }
    else printf("Test2 OK: ray hit triangle at t=%.2f\n", t);

    // Off-target ray (shifted well to the side) must miss.
    Ray ray_off = RayFromNDC(inv_vp, ndc_x + 0.5f, ndc_y);
    float t_off;
    bool hit_off = RayHitsTriangle(ray_off.origin, ray_off.dir, v0, v1, v2, t_off);
    if (hit_off) { printf("Test3 FAILED: off-target ray unexpectedly hit the triangle\n"); failures++; }
    else printf("Test3 OK: off-target ray missed as expected\n");

    // A point behind the camera must report false from ProjectPoint.
    Vec3 behind = eye + (eye - target).Normalized() * 100.0f; // further behind the eye, away from target
    Vec3 behind_screen;
    bool behind_ok = ProjectPoint(view_proj, behind, vw, vh, behind_screen);
    if (behind_ok) { printf("Test4 FAILED: ProjectPoint returned true for a point behind the camera\n"); failures++; }
    else printf("Test4 OK: point behind the camera correctly rejected\n");

    // --- DistancePointToSegment2D ---
    float d0 = DistancePointToSegment2D(5, 0, 0, 0, 10, 0);
    if (std::abs(d0) > 1e-4f) { printf("Test5 FAILED: expected 0, got %f\n", d0); failures++; }
    else printf("Test5 OK: on-segment distance is 0\n");

    float d1 = DistancePointToSegment2D(5, 3, 0, 0, 10, 0);
    if (std::abs(d1 - 3.0f) > 1e-4f) { printf("Test6 FAILED: expected 3, got %f\n", d1); failures++; }
    else printf("Test6 OK: perpendicular distance to midpoint is 3\n");

    float d2 = DistancePointToSegment2D(20, 0, 0, 0, 10, 0);
    if (std::abs(d2 - 10.0f) > 1e-4f) { printf("Test7 FAILED: expected 10, got %f\n", d2); failures++; }
    else printf("Test7 OK: beyond-endpoint distance clamps correctly\n");

    printf(failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d TEST(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
