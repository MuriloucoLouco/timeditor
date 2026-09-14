#pragma once
#include "math3d.h"
#include <algorithm>
#include <cmath>

// Shared screen-space <-> world-space math for 3D picking: unprojecting a
// mouse position into a world ray (for face/triangle picking) and
// projecting a world point back to screen space (for vertex/edge/box-select
// picking, which is done by comparing screen-space positions rather than
// ray-casting a zero-size point - simpler and more robust).
//
// Originally written once, locally, for the TMD Viewer's now-removed
// "click a face to jump to its primitive" feature (tmd_panel.cpp); promoted
// here so the Model Editor's picking can reuse it instead of re-deriving
// the same unproject/intersection math a second time.
namespace gfx {

struct Ray {
    Vec3 origin;
    Vec3 dir; // normalized
};

// Unprojects a clip-space point (x,y in NDC, z = -1 for the near plane or
// +1 for the far plane) back to world space through the full 4x4 inverse -
// needed (rather than the affine-only TransformPoint) because a
// perspective projection's last row isn't (0,0,0,1), so this must carry
// the w component through a perspective divide.
inline Vec3 UnprojectPoint(const Mat4& inv_view_proj, Vec3 ndc) {
    const float* m = inv_view_proj.m;
    float x = m[0] * ndc.x + m[4] * ndc.y + m[8] * ndc.z + m[12];
    float y = m[1] * ndc.x + m[5] * ndc.y + m[9] * ndc.z + m[13];
    float z = m[2] * ndc.x + m[6] * ndc.y + m[10] * ndc.z + m[14];
    float w = m[3] * ndc.x + m[7] * ndc.y + m[11] * ndc.z + m[15];
    if (std::abs(w) < 1e-8f) w = 1e-8f;
    return { x / w, y / w, z / w };
}

// Builds a world-space ray through the near/far planes at a given NDC
// (x,y) - the mouse position converted to [-1,1] on both axes.
inline Ray RayFromNDC(const Mat4& inv_view_proj, float ndc_x, float ndc_y) {
    Vec3 near_point = UnprojectPoint(inv_view_proj, { ndc_x, ndc_y, -1.0f });
    Vec3 far_point = UnprojectPoint(inv_view_proj, { ndc_x, ndc_y, 1.0f });
    return { near_point, (far_point - near_point).Normalized() };
}

// Standard Möller-Trumbore ray/triangle test.
inline bool RayHitsTriangle(Vec3 origin, Vec3 dir, Vec3 v0, Vec3 v1, Vec3 v2, float& out_t) {
    constexpr float kEps = 1e-6f;
    Vec3 edge1 = v1 - v0, edge2 = v2 - v0;
    Vec3 h = dir.Cross(edge2);
    float a = edge1.Dot(h);
    if (std::abs(a) < kEps) return false;
    float f = 1.0f / a;
    Vec3 s = origin - v0;
    float u = f * s.Dot(h);
    if (u < 0.0f || u > 1.0f) return false;
    Vec3 q = s.Cross(edge1);
    float v = f * dir.Dot(q);
    if (v < 0.0f || u + v > 1.0f) return false;
    float t = f * edge2.Dot(q);
    if (t <= kEps) return false;
    out_t = t;
    return true;
}

// Projects a world point to pixel coordinates within a `viewport_w` x
// `viewport_h` viewport (origin top-left, matching ImGui's own screen
// space) - the inverse of RayFromNDC/UnprojectPoint. Returns false if the
// point is behind the camera (w <= 0), in which case `out_screen` is left
// unset - the caller should treat that vertex/point as unpickable this
// frame rather than plot a meaningless projection.
inline bool ProjectPoint(const Mat4& view_proj, Vec3 world, float viewport_w, float viewport_h, Vec3& out_screen) {
    const float* m = view_proj.m;
    float x = m[0] * world.x + m[4] * world.y + m[8] * world.z + m[12];
    float y = m[1] * world.x + m[5] * world.y + m[9] * world.z + m[13];
    float z = m[2] * world.x + m[6] * world.y + m[10] * world.z + m[14];
    float w = m[3] * world.x + m[7] * world.y + m[11] * world.z + m[15];
    if (w <= 1e-6f) return false;
    float ndc_x = x / w, ndc_y = y / w, ndc_z = z / w;
    out_screen = { (ndc_x * 0.5f + 0.5f) * viewport_w, (1.0f - (ndc_y * 0.5f + 0.5f)) * viewport_h, ndc_z };
    return true;
}

// Intersects a ray with an infinite plane (given by a point on it and its
// normal). Returns false if the ray is parallel to the plane (or, given
// `origin`/`dir` from RayFromNDC, effectively never will be in practice).
// Used by the Model Editor's direct-drag transform: moving a selection
// tracks the mouse across the plane through the selection's centroid
// facing the camera, so a drag stays perspective-correct (near objects
// move less per pixel than far ones) instead of using a flat pixel-to-
// world scale that would feel wrong across zoom levels.
inline bool RayHitsPlane(Vec3 origin, Vec3 dir, Vec3 plane_point, Vec3 plane_normal, float& out_t) {
    float denom = dir.Dot(plane_normal);
    if (std::abs(denom) < 1e-6f) return false;
    out_t = (plane_point - origin).Dot(plane_normal) / denom;
    return true;
}

// Shortest distance from point `p` to the segment [a,b], all in the same
// 2D (typically screen-pixel) space - used for edge-mode picking (nearest
// edge to the cursor) and hover-testing UV/screen segments generally.
inline float DistancePointToSegment2D(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    float len_sq = dx * dx + dy * dy;
    float t = (len_sq > 1e-9f) ? ((px - ax) * dx + (py - ay) * dy) / len_sq : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    float cx = ax + t * dx, cy = ay + t * dy;
    float ex = px - cx, ey = py - cy;
    return std::sqrt(ex * ex + ey * ey);
}

} // namespace gfx
