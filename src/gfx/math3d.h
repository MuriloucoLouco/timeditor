#pragma once
#include <cmath>

namespace gfx {

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }

    float Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 Cross(const Vec3& o) const {
        return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x };
    }
    float Length() const { return std::sqrt(Dot(*this)); }
    Vec3 Normalized() const {
        float len = Length();
        return len > 1e-6f ? (*this) * (1.0f / len) : Vec3{ 0, 0, 0 };
    }
};

// Column-major 4x4 matrix (OpenGL convention: m[col * 4 + row]), so an
// instance's raw float data can be passed straight to glLoadMatrixf/
// glMultMatrixf.
struct Mat4 {
    float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

    static Mat4 Identity() { return Mat4{}; }

    static Mat4 Translate(Vec3 t) {
        Mat4 r;
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }

    static Mat4 Scale(Vec3 s) {
        Mat4 r;
        r.m[0] = s.x;
        r.m[5] = s.y;
        r.m[10] = s.z;
        return r;
    }

    static Mat4 RotateX(float radians) {
        Mat4 r;
        float c = std::cos(radians), s = std::sin(radians);
        r.m[5] = c; r.m[6] = s;
        r.m[9] = -s; r.m[10] = c;
        return r;
    }

    static Mat4 RotateY(float radians) {
        Mat4 r;
        float c = std::cos(radians), s = std::sin(radians);
        r.m[0] = c; r.m[2] = -s;
        r.m[8] = s; r.m[10] = c;
        return r;
    }

    static Mat4 RotateZ(float radians) {
        Mat4 r;
        float c = std::cos(radians), s = std::sin(radians);
        r.m[0] = c; r.m[1] = s;
        r.m[4] = -s; r.m[5] = c;
        return r;
    }

    static Mat4 Perspective(float fov_y_radians, float aspect, float z_near, float z_far) {
        Mat4 r;
        for (int i = 0; i < 16; i++) r.m[i] = 0;
        float f = 1.0f / std::tan(fov_y_radians * 0.5f);
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (z_far + z_near) / (z_near - z_far);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * z_far * z_near) / (z_near - z_far);
        return r;
    }

    static Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up) {
        Vec3 f = (target - eye).Normalized();
        Vec3 s = f.Cross(up).Normalized();
        Vec3 u = s.Cross(f);

        Mat4 r;
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -s.Dot(eye);
        r.m[13] = -u.Dot(eye);
        r.m[14] = f.Dot(eye);
        return r;
    }

    // this * o (column-major, matches glMultMatrixf semantics: applying the
    // result to a vector applies `o` first, then `this`).
    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                float sum = 0;
                for (int k = 0; k < 4; k++) sum += m[k * 4 + row] * o.m[col * 4 + k];
                r.m[col * 4 + row] = sum;
            }
        }
        return r;
    }

    // General 4x4 inverse via cofactor expansion. Used to unproject a mouse
    // position into a world-space ray for 3D-viewport picking; returns the
    // identity if the matrix is singular (view/projection matrices built by
    // this class never are in practice).
    Mat4 Inverse() const {
        const float* a = m;
        float inv[16];

        inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] +
                 a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
        inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] -
                 a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
        inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] +
                 a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
        inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] -
                  a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
        inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] -
                 a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
        inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] +
                 a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
        inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] -
                 a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
        inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] +
                  a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
        inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
                 a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
        inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] -
                 a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
        inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] +
                  a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
        inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] -
                  a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
        inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] -
                 a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
        inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
                 a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
        inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
                  a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
        inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
                  a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

        float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        if (std::abs(det) < 1e-9f) return Identity();

        Mat4 r;
        float inv_det = 1.0f / det;
        for (int i = 0; i < 16; i++) r.m[i] = inv[i] * inv_det;
        return r;
    }
};

// Transforms a point (translation applies).
inline Vec3 TransformPoint(const Mat4& m, Vec3 v) {
    return { m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12],
             m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13],
             m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] };
}

// Transforms a direction (e.g. a normal) - the 3x3 part only, no translation.
inline Vec3 TransformDirection(const Mat4& m, Vec3 v) {
    return { m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
             m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
             m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z };
}

} // namespace gfx
