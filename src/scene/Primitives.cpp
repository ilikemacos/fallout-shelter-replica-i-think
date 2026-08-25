#include "scene/Primitives.hpp"
#include <cmath>

namespace hv::scene {

void appendBox(MeshBuild& m, Vec3 c, Vec3 h, Vec2 uvT, u32 color) {
    struct Face { Vec3 n; Vec3 u; Vec3 v; };
    const Face faces[6] = {
        {{ 1,0,0}, {0,0,-1}, {0,1,0}}, {{-1,0,0}, {0,0,1}, {0,1,0}},
        {{0, 1,0}, {1,0,0}, {0,0,-1}}, {{0,-1,0}, {1,0,0}, {0,0,1}},
        {{0,0, 1}, {1,0,0}, {0,1,0}}, {{0,0,-1}, {-1,0,0}, {0,1,0}},
    };
    for (const Face& f : faces) {
        const u32 base = static_cast<u32>(m.v.size());
        const Vec3 center = c + f.n * Vec3{h.x, h.y, h.z} * (f.n.x != 0 ? std::fabs(f.n.x) : (f.n.y != 0 ? std::fabs(f.n.y) : std::fabs(f.n.z)));
        const Vec3 nAbs{std::fabs(f.n.x) * h.x, std::fabs(f.n.y) * h.y, std::fabs(f.n.z) * h.z};
        const f32 extentU = f.n.x != 0 ? h.z : (f.n.y != 0 ? h.x : h.x);
        const f32 extentV = f.n.x != 0 ? h.y : (f.n.y != 0 ? h.z : h.y);
        const Vec3 corners[4] = {
            center - f.u * extentU - f.v * extentV,
            center + f.u * extentU - f.v * extentV,
            center + f.u * extentU + f.v * extentV,
            center - f.u * extentU + f.v * extentV,
        };
        const Vec2 uvs[4] = {{0,0}, {uvT.x,0}, {uvT.x,uvT.y}, {0,uvT.y}};
        for (int k = 0; k < 4; ++k) {
            Vertex vert; vert.position = corners[k]; vert.normal = f.n; vert.tangent = f.u;
            vert.uv = uvs[k]; vert.color = color;
            m.v.push_back(vert);
        }
        (void)nAbs;
        m.i.insert(m.i.end(), {base, base+1, base+2, base, base+2, base+3});
    }
}

void appendCylinder(MeshBuild& m, Vec3 base, f32 radius, f32 height, int segments, u32 color, bool caps) {
    segments = std::max(3, segments);
    const u32 startIdx = static_cast<u32>(m.v.size());
    for (int i = 0; i <= segments; ++i) {
        const f32 t = static_cast<f32>(i) / static_cast<f32>(segments);
        const f32 a = t * kTwoPi;
        const Vec3 n{std::cos(a), 0, std::sin(a)};
        for (int y = 0; y < 2; ++y) {
            Vertex v; v.position = base + n * radius + Vec3{0, height * static_cast<f32>(y), 0};
            v.normal = n; v.tangent = Vec3{-n.z, 0, n.x}; v.uv = {t * 4.0f, static_cast<f32>(y)};
            v.color = color;
            m.v.push_back(v);
        }
    }
    for (int i = 0; i < segments; ++i) {
        const u32 a = startIdx + static_cast<u32>(i) * 2;
        const u32 b = a + 2;
        m.i.insert(m.i.end(), {a, a+1, b, b, a+1, b+1});
    }
    if (caps) {
        auto cap = [&](f32 y, Vec3 n, bool flip) {
            const u32 c = static_cast<u32>(m.v.size());
            Vertex center; center.position = base + Vec3{0, y, 0}; center.normal = n; center.uv = {0.5f, 0.5f}; center.color = color;
            m.v.push_back(center);
            for (int i = 0; i <= segments; ++i) {
                const f32 t = static_cast<f32>(i) / static_cast<f32>(segments);
                const f32 a = t * kTwoPi;
                Vertex v; v.position = base + Vec3{std::cos(a) * radius, y, std::sin(a) * radius};
                v.normal = n; v.uv = {std::cos(a) * 0.5f + 0.5f, std::sin(a) * 0.5f + 0.5f}; v.color = color;
                m.v.push_back(v);
            }
            for (int i = 0; i < segments; ++i) {
                if (flip) m.i.insert(m.i.end(), {c, c + static_cast<u32>(i) + 2, c + static_cast<u32>(i) + 1});
                else m.i.insert(m.i.end(), {c, c + static_cast<u32>(i) + 1, c + static_cast<u32>(i) + 2});
            }
        };
        // Ring vertices run counter-clockwise when viewed from +Y, so the
        // un-flipped fan winds downward: the bottom cap (normal -Y) wants it
        // as-is and the top cap (normal +Y) is the one that must flip. Having
        // these the other way round left both caps back-face culled, so every
        // drum, tank and pipe end was an open hole.
        cap(0.0f, Vec3{0,-1,0}, false);
        cap(height, Vec3{0,1,0}, true);
    }
}

void appendCapsule(MeshBuild& m, Vec3 base, f32 radius, f32 height, int segments, u32 color) {
    segments = std::max(4, segments);
    const int rings = std::max(2, segments / 2);
    const f32 cylHeight = std::max(0.0f, height - radius * 2.0f);
    const Vec3 cylBase = base + Vec3{0, radius, 0};

    auto hemisphere = [&](Vec3 origin, f32 sign) {
        const u32 startIdx = static_cast<u32>(m.v.size());
        for (int r = 0; r <= rings; ++r) {
            const f32 phi = (static_cast<f32>(r) / static_cast<f32>(rings)) * kHalfPi;
            const f32 y = std::sin(phi) * radius * sign;
            const f32 rr = std::cos(phi) * radius;
            for (int i = 0; i <= segments; ++i) {
                const f32 a = (static_cast<f32>(i) / static_cast<f32>(segments)) * kTwoPi;
                const Vec3 dir{std::cos(a) * rr, y, std::sin(a) * rr};
                Vertex v; v.position = origin + dir;
                v.normal = normalize(Vec3{std::cos(a) * std::cos(phi), std::sin(phi) * sign, std::sin(a) * std::cos(phi)});
                v.uv = {static_cast<f32>(i) / segments, static_cast<f32>(r) / rings};
                v.color = color;
                m.v.push_back(v);
            }
        }
        for (int r = 0; r < rings; ++r) {
            for (int i = 0; i < segments; ++i) {
                const u32 a = startIdx + static_cast<u32>(r) * static_cast<u32>(segments + 1) + static_cast<u32>(i);
                const u32 b = a + static_cast<u32>(segments + 1);
                if (sign > 0) m.i.insert(m.i.end(), {a, b, a+1, a+1, b, b+1});
                else m.i.insert(m.i.end(), {a, a+1, b, a+1, b+1, b});
            }
        }
    };
    hemisphere(cylBase, 1.0f);
    hemisphere(cylBase + Vec3{0, cylHeight, 0}, -1.0f);
    appendCylinder(m, cylBase, radius, cylHeight, segments, color, false);
}

void appendQuadXZ(MeshBuild& m, Vec3 center, f32 width, f32 depth, Vec2 uvT, u32 color) {
    const u32 base = static_cast<u32>(m.v.size());
    const f32 hw = width * 0.5f, hd = depth * 0.5f;
    const Vec3 pts[4] = {{-hw,0,-hd}, {hw,0,-hd}, {hw,0,hd}, {-hw,0,hd}};
    const Vec2 uvs[4] = {{0,0}, {uvT.x,0}, {uvT.x,uvT.y}, {0,uvT.y}};
    for (int i = 0; i < 4; ++i) {
        Vertex v; v.position = center + pts[i]; v.normal = {0,1,0}; v.tangent = {1,0,0};
        v.uv = uvs[i]; v.color = color;
        m.v.push_back(v);
    }
    // Wound counter-clockwise as seen from +Y (the direction the vertex
    // normal points), so the lit face is the one that survives back-face
    // culling. The naive 0-1-2 / 0-2-3 order gives the opposite winding here
    // and makes every floor in the game invisible from above.
    m.i.insert(m.i.end(), {base, base+2, base+1, base, base+3, base+2});
}

void appendQuadXY(MeshBuild& m, Vec3 center, f32 width, f32 height, Vec2 uvT, u32 color) {
    const u32 base = static_cast<u32>(m.v.size());
    const f32 hw = width * 0.5f, hh = height * 0.5f;
    const Vec3 pts[4] = {{-hw,-hh,0}, {hw,-hh,0}, {hw,hh,0}, {-hw,hh,0}};
    const Vec2 uvs[4] = {{0,0}, {uvT.x,0}, {uvT.x,uvT.y}, {0,uvT.y}};
    for (int i = 0; i < 4; ++i) {
        Vertex v; v.position = center + pts[i]; v.normal = {0,0,1}; v.tangent = {1,0,0};
        v.uv = uvs[i]; v.color = color;
        m.v.push_back(v);
    }
    m.i.insert(m.i.end(), {base, base+1, base+2, base, base+2, base+3});
}

void appendPipe(MeshBuild& m, Vec3 a, Vec3 b, f32 radius, int segments, u32 color) {
    const Vec3 dir = b - a;
    const f32 len = length(dir);
    if (len < 1e-5f) return;
    const Vec3 up = std::fabs(dir.y / len) > 0.99f ? Vec3{1,0,0} : Vec3{0,1,0};
    const Vec3 fwd = dir * (1.0f / len);
    const Vec3 right = normalize(cross(fwd, up));
    const Vec3 realUp = cross(right, fwd);
    const u32 startIdx = static_cast<u32>(m.v.size());
    for (int seg = 0; seg <= 1; ++seg) {
        const Vec3 center = seg == 0 ? a : b;
        for (int i = 0; i <= segments; ++i) {
            const f32 t = static_cast<f32>(i) / static_cast<f32>(segments);
            const f32 ang = t * kTwoPi;
            const Vec3 n = right * std::cos(ang) + realUp * std::sin(ang);
            Vertex v; v.position = center + n * radius; v.normal = n; v.tangent = fwd;
            v.uv = {t * 2.0f, static_cast<f32>(seg)}; v.color = color;
            m.v.push_back(v);
        }
    }
    for (int i = 0; i < segments; ++i) {
        const u32 aI = startIdx + static_cast<u32>(i);
        const u32 bI = aI + static_cast<u32>(segments + 1);
        m.i.insert(m.i.end(), {aI, bI, aI+1, aI+1, bI, bI+1});
    }
}

void recalcNormalsFlat(MeshBuild& m) {
    for (size_t t = 0; t + 2 < m.i.size(); t += 3) {
        Vertex& v0 = m.v[m.i[t]]; Vertex& v1 = m.v[m.i[t+1]]; Vertex& v2 = m.v[m.i[t+2]];
        const Vec3 n = normalize(cross(v1.position - v0.position, v2.position - v0.position));
        v0.normal = v1.normal = v2.normal = n;
    }
}

} // namespace hv::scene
