#pragma once
// Small, dependency-free linear algebra. Right-handed, column-vector
// convention; Mat4 is column-major so it uploads to GL/Vulkan unchanged.
#include "core/Types.hpp"
#include <cmath>
#include <algorithm>

namespace hv {

constexpr f32 kPi     = 3.14159265358979323846f;
constexpr f32 kTwoPi  = 2.0f * kPi;
constexpr f32 kHalfPi = 0.5f * kPi;
constexpr f32 kDeg2Rad = kPi / 180.0f;
constexpr f32 kRad2Deg = 180.0f / kPi;

inline f32 clampf(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline f32 saturate(f32 v) { return clampf(v, 0.0f, 1.0f); }
inline f32 lerpf(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
inline f32 smoothstepf(f32 t) { t = saturate(t); return t * t * (3.0f - 2.0f * t); }
/// Frame-rate independent exponential approach; `rate` is the fraction of the
/// remaining distance removed per second.
inline f32 damp(f32 a, f32 b, f32 rate, f32 dt) {
    return lerpf(a, b, 1.0f - std::exp(-rate * dt));
}
inline f32 wrapAngle(f32 a) {
    while (a >  kPi) a -= kTwoPi;
    while (a < -kPi) a += kTwoPi;
    return a;
}

struct Vec2 {
    f32 x = 0, y = 0;
    constexpr Vec2() = default;
    constexpr Vec2(f32 x_, f32 y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(f32 s) const { return {x * s, y * s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
};

struct Vec3 {
    f32 x = 0, y = 0, z = 0;
    constexpr Vec3() = default;
    constexpr Vec3(f32 v) : x(v), y(v), z(v) {}
    constexpr Vec3(f32 x_, f32 y_, f32 z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(f32 s) const { return {x * s, y * s, z * s}; }
    Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3 operator/(f32 s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(f32 s) { x *= s; y *= s; z *= s; return *this; }
    f32  operator[](int i) const { return (&x)[i]; }
    f32& operator[](int i) { return (&x)[i]; }
};

inline f32  dot(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
inline f32  length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline f32  lengthSq(const Vec3& v) { return dot(v, v); }
inline Vec3 normalize(const Vec3& v) {
    const f32 l = length(v);
    return l > 1e-8f ? v * (1.0f / l) : Vec3{0, 0, 0};
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, f32 t) { return a + (b - a) * t; }
inline Vec3 minv(const Vec3& a, const Vec3& b) {
    return {std::min(a.x,b.x), std::min(a.y,b.y), std::min(a.z,b.z)};
}
inline Vec3 maxv(const Vec3& a, const Vec3& b) {
    return {std::max(a.x,b.x), std::max(a.y,b.y), std::max(a.z,b.z)};
}
inline f32 distance(const Vec3& a, const Vec3& b) { return length(a - b); }
inline f32 distanceSq(const Vec3& a, const Vec3& b) { return lengthSq(a - b); }

struct Vec4 {
    f32 x = 0, y = 0, z = 0, w = 0;
    constexpr Vec4() = default;
    constexpr Vec4(f32 x_, f32 y_, f32 z_, f32 w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(const Vec3& v, f32 w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    Vec3 xyz() const { return {x, y, z}; }
    f32  operator[](int i) const { return (&x)[i]; }
    f32& operator[](int i) { return (&x)[i]; }
    Vec4 operator+(const Vec4& o) const { return {x+o.x, y+o.y, z+o.z, w+o.w}; }
    Vec4 operator*(f32 s) const { return {x*s, y*s, z*s, w*s}; }
};

/// Column-major 4x4. m[c][r]; memory order matches glUniformMatrix4fv(transpose=false).
struct Mat4 {
    f32 m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

    static Mat4 identity() { return Mat4{}; }
    static Mat4 zero() { Mat4 r; for (int i = 0; i < 16; ++i) r.m[i] = 0; return r; }

    f32  at(int c, int r) const { return m[c * 4 + r]; }
    f32& at(int c, int r) { return m[c * 4 + r]; }

    static Mat4 translate(const Vec3& t) {
        Mat4 r; r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r;
    }
    static Mat4 scale(const Vec3& s) {
        Mat4 r; r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; return r;
    }
    static Mat4 rotationX(f32 a) {
        Mat4 r; const f32 c = std::cos(a), s = std::sin(a);
        r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r;
    }
    static Mat4 rotationY(f32 a) {
        Mat4 r; const f32 c = std::cos(a), s = std::sin(a);
        r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r;
    }
    static Mat4 rotationZ(f32 a) {
        Mat4 r; const f32 c = std::cos(a), s = std::sin(a);
        r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r = Mat4::zero();
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k) {
                const f32 b = o.m[c * 4 + k];
                if (b == 0.0f) continue;
                for (int rr = 0; rr < 4; ++rr) r.m[c * 4 + rr] += m[k * 4 + rr] * b;
            }
        return r;
    }
    Vec4 operator*(const Vec4& v) const {
        Vec4 r;
        for (int i = 0; i < 4; ++i)
            r[i] = m[0*4+i]*v.x + m[1*4+i]*v.y + m[2*4+i]*v.z + m[3*4+i]*v.w;
        return r;
    }
    Vec3 transformPoint(const Vec3& p) const { return (*this * Vec4(p, 1.0f)).xyz(); }
    Vec3 transformDir(const Vec3& d) const { return (*this * Vec4(d, 0.0f)).xyz(); }

    static Mat4 perspective(f32 fovYRadians, f32 aspect, f32 zn, f32 zf) {
        const f32 f = 1.0f / std::tan(fovYRadians * 0.5f);
        Mat4 r = Mat4::zero();
        r.m[0] = f / aspect; r.m[5] = f;
        r.m[10] = (zf + zn) / (zn - zf); r.m[11] = -1.0f;
        r.m[14] = (2.0f * zf * zn) / (zn - zf);
        return r;
    }
    static Mat4 orthographic(f32 l, f32 r_, f32 b, f32 t, f32 zn, f32 zf) {
        Mat4 r = Mat4::identity();
        r.m[0] = 2.0f / (r_ - l); r.m[5] = 2.0f / (t - b); r.m[10] = -2.0f / (zf - zn);
        r.m[12] = -(r_ + l) / (r_ - l);
        r.m[13] = -(t + b) / (t - b);
        r.m[14] = -(zf + zn) / (zf - zn);
        return r;
    }
    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        const Vec3 f = normalize(center - eye);
        Vec3 s = cross(f, up);
        if (lengthSq(s) < 1e-12f) s = cross(f, Vec3{0, 0, 1});
        s = normalize(s);
        const Vec3 u = cross(s, f);
        Mat4 r = Mat4::identity();
        r.m[0]=s.x; r.m[4]=s.y; r.m[8]=s.z;
        r.m[1]=u.x; r.m[5]=u.y; r.m[9]=u.z;
        r.m[2]=-f.x; r.m[6]=-f.y; r.m[10]=-f.z;
        r.m[12]=-dot(s, eye); r.m[13]=-dot(u, eye); r.m[14]=dot(f, eye);
        return r;
    }
    /// Inverse of an affine transform built from rotation/scale + translation.
    Mat4 affineInverse() const {
        Mat4 r = Mat4::identity();
        f32 a[3][3];
        for (int c = 0; c < 3; ++c) for (int rr = 0; rr < 3; ++rr) a[c][rr] = at(c, rr);
        const f32 det =
            a[0][0]*(a[1][1]*a[2][2]-a[2][1]*a[1][2]) -
            a[1][0]*(a[0][1]*a[2][2]-a[2][1]*a[0][2]) +
            a[2][0]*(a[0][1]*a[1][2]-a[1][1]*a[0][2]);
        if (std::fabs(det) < 1e-12f) return r;
        const f32 id = 1.0f / det;
        f32 inv[3][3];
        inv[0][0] =  (a[1][1]*a[2][2]-a[2][1]*a[1][2]) * id;
        inv[1][0] = -(a[1][0]*a[2][2]-a[2][0]*a[1][2]) * id;
        inv[2][0] =  (a[1][0]*a[2][1]-a[2][0]*a[1][1]) * id;
        inv[0][1] = -(a[0][1]*a[2][2]-a[2][1]*a[0][2]) * id;
        inv[1][1] =  (a[0][0]*a[2][2]-a[2][0]*a[0][2]) * id;
        inv[2][1] = -(a[0][0]*a[2][1]-a[2][0]*a[0][1]) * id;
        inv[0][2] =  (a[0][1]*a[1][2]-a[1][1]*a[0][2]) * id;
        inv[1][2] = -(a[0][0]*a[1][2]-a[1][0]*a[0][2]) * id;
        inv[2][2] =  (a[0][0]*a[1][1]-a[1][0]*a[0][1]) * id;
        for (int c = 0; c < 3; ++c) for (int rr = 0; rr < 3; ++rr) r.at(c, rr) = inv[c][rr];
        const Vec3 t{m[12], m[13], m[14]};
        r.m[12] = -(inv[0][0]*t.x + inv[1][0]*t.y + inv[2][0]*t.z);
        r.m[13] = -(inv[0][1]*t.x + inv[1][1]*t.y + inv[2][1]*t.z);
        r.m[14] = -(inv[0][2]*t.x + inv[1][2]*t.y + inv[2][2]*t.z);
        return r;
    }
    /// Full general inverse (needed to un-project through a projection matrix).
    Mat4 inverse() const {
        f32 inv[16];
        const f32* a = m;
        inv[0]  =  a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
        inv[4]  = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
        inv[8]  =  a[4]*a[9]*a[15]  - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
        inv[12] = -a[4]*a[9]*a[14]  + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
        inv[1]  = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
        inv[5]  =  a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
        inv[9]  = -a[0]*a[9]*a[15]  + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
        inv[13] =  a[0]*a[9]*a[14]  - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
        inv[2]  =  a[1]*a[6]*a[15]  - a[1]*a[7]*a[14]  - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7]  - a[13]*a[3]*a[6];
        inv[6]  = -a[0]*a[6]*a[15]  + a[0]*a[7]*a[14]  + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7]  + a[12]*a[3]*a[6];
        inv[10] =  a[0]*a[5]*a[15]  - a[0]*a[7]*a[13]  - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7]  - a[12]*a[3]*a[5];
        inv[14] = -a[0]*a[5]*a[14]  + a[0]*a[6]*a[13]  + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6]  + a[12]*a[2]*a[5];
        inv[3]  = -a[1]*a[6]*a[11]  + a[1]*a[7]*a[10]  + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7]   + a[9]*a[3]*a[6];
        inv[7]  =  a[0]*a[6]*a[11]  - a[0]*a[7]*a[10]  - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7]   - a[8]*a[3]*a[6];
        inv[11] = -a[0]*a[5]*a[11]  + a[0]*a[7]*a[9]   + a[4]*a[1]*a[11] - a[4]*a[3]*a[9]  - a[8]*a[1]*a[7]   + a[8]*a[3]*a[5];
        inv[15] =  a[0]*a[5]*a[10]  - a[0]*a[6]*a[9]   - a[4]*a[1]*a[10] + a[4]*a[2]*a[9]  + a[8]*a[1]*a[6]   - a[8]*a[2]*a[5];
        f32 det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
        if (std::fabs(det) < 1e-20f) return Mat4::identity();
        det = 1.0f / det;
        Mat4 r;
        for (int i = 0; i < 16; ++i) r.m[i] = inv[i] * det;
        return r;
    }
};

struct AABB {
    Vec3 min{ 1e30f,  1e30f,  1e30f};
    Vec3 max{-1e30f, -1e30f, -1e30f};
    void expand(const Vec3& p) { min = minv(min, p); max = maxv(max, p); }
    void expand(const AABB& b) { min = minv(min, b.min); max = maxv(max, b.max); }
    bool valid() const { return min.x <= max.x; }
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 extents() const { return (max - min) * 0.5f; }
    f32  radius() const { return length(extents()); }
    bool contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y &&
               p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }
    bool intersects(const AABB& o) const {
        return min.x <= o.max.x && max.x >= o.min.x &&
               min.y <= o.max.y && max.y >= o.min.y &&
               min.z <= o.max.z && max.z >= o.min.z;
    }
    AABB transformed(const Mat4& x) const {
        AABB r;
        for (int i = 0; i < 8; ++i) {
            const Vec3 c{ (i & 1) ? max.x : min.x,
                          (i & 2) ? max.y : min.y,
                          (i & 4) ? max.z : min.z };
            r.expand(x.transformPoint(c));
        }
        return r;
    }
};

struct Plane {
    Vec3 n{0, 1, 0};
    f32  d = 0;
    f32 distance(const Vec3& p) const { return dot(n, p) + d; }
};

/// Six-plane frustum extracted from a view-projection matrix (Gribb/Hartmann).
struct Frustum {
    Plane planes[6];

    static Frustum fromViewProj(const Mat4& vp) {
        Frustum f;
        auto row = [&](int r) { return Vec4{vp.at(0,r), vp.at(1,r), vp.at(2,r), vp.at(3,r)}; };
        const Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        const Vec4 raw[6] = {
            {r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w},  // left
            {r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w},  // right
            {r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w},  // bottom
            {r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w},  // top
            {r3.x + r2.x, r3.y + r2.y, r3.z + r2.z, r3.w + r2.w},  // near
            {r3.x - r2.x, r3.y - r2.y, r3.z - r2.z, r3.w - r2.w},  // far
        };
        for (int i = 0; i < 6; ++i) {
            Vec3 n{raw[i].x, raw[i].y, raw[i].z};
            const f32 l = length(n);
            if (l > 1e-8f) { f.planes[i].n = n * (1.0f / l); f.planes[i].d = raw[i].w / l; }
        }
        return f;
    }
    bool intersects(const AABB& b) const {
        for (const Plane& p : planes) {
            const Vec3 pv{ p.n.x >= 0 ? b.max.x : b.min.x,
                           p.n.y >= 0 ? b.max.y : b.min.y,
                           p.n.z >= 0 ? b.max.z : b.min.z };
            if (p.distance(pv) < 0.0f) return false;
        }
        return true;
    }
    bool intersectsSphere(const Vec3& c, f32 r) const {
        for (const Plane& p : planes) if (p.distance(c) < -r) return false;
        return true;
    }
};

struct Ray {
    Vec3 origin;
    Vec3 dir{0, 0, -1};
    Vec3 at(f32 t) const { return origin + dir * t; }
    /// Slab test; returns false when the box is behind or missed.
    bool intersectAABB(const AABB& b, f32& tOut) const {
        f32 tmin = 0.0f, tmax = 1e30f;
        for (int i = 0; i < 3; ++i) {
            const f32 d = dir[i];
            if (std::fabs(d) < 1e-8f) {
                if (origin[i] < b.min[i] || origin[i] > b.max[i]) return false;
            } else {
                const f32 inv = 1.0f / d;
                f32 t1 = (b.min[i] - origin[i]) * inv;
                f32 t2 = (b.max[i] - origin[i]) * inv;
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if (tmin > tmax) return false;
            }
        }
        tOut = tmin;
        return true;
    }
    /// Intersection with an axis-aligned plane y = planeY.
    bool intersectPlaneY(f32 planeY, f32& tOut) const {
        if (std::fabs(dir.y) < 1e-8f) return false;
        const f32 t = (planeY - origin.y) / dir.y;
        if (t < 0.0f) return false;
        tOut = t;
        return true;
    }
};

/// sRGB hex literal (0xRRGGBB) to linear-space colour.
inline Vec3 colorFromHex(u32 hex) {
    auto ch = [](u32 v) {
        const f32 s = static_cast<f32>(v) / 255.0f;
        return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
    };
    return { ch((hex >> 16) & 0xFF), ch((hex >> 8) & 0xFF), ch(hex & 0xFF) };
}

} // namespace hv
