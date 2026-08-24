#pragma once
// CPU-side procedural mesh generation. Every visible thing in Haven —
// corridors, machinery, furniture, residents — is built from these
// primitives with per-piece material variation, so no modelled asset from
// any existing game is ever needed.
#include "renderer/RenderTypes.hpp"
#include "core/Random.hpp"

namespace hv::scene {

using namespace hv::gfx;

struct MeshBuild {
    std::vector<Vertex> v;
    std::vector<u32> i;
    MeshDesc toDesc(const char* name, bool dynamic = false) const {
        MeshDesc d; d.vertices = v; d.indices = i; d.dynamic = dynamic; d.debugName = name;
        return d;
    }
};

/// Appends a box centred at `center` with the given half-extents, all faces
/// outward-facing with correct normals and per-face UV tiling.
void appendBox(MeshBuild& m, Vec3 center, Vec3 halfExtents, Vec2 uvTiling = {1, 1}, u32 color = 0xFFFFFFFFu);
/// A cylinder standing along +Y — pipes, pillars, generator drums, lamp posts.
void appendCylinder(MeshBuild& m, Vec3 base, f32 radius, f32 height, int segments,
                    u32 color = 0xFFFFFFFFu, bool caps = true);
/// A capsule along +Y — the resident body's torso/limb primitive.
void appendCapsule(MeshBuild& m, Vec3 base, f32 radius, f32 height, int segments, u32 color = 0xFFFFFFFFu);
/// A flat quad on the XZ plane facing +Y — floors, ceilings, screens.
void appendQuadXZ(MeshBuild& m, Vec3 center, f32 width, f32 depth, Vec2 uvTiling = {1, 1}, u32 color = 0xFFFFFFFFu);
/// A vertical quad facing +Z — walls, doors, posters.
void appendQuadXY(MeshBuild& m, Vec3 center, f32 width, f32 height, Vec2 uvTiling = {1, 1}, u32 color = 0xFFFFFFFFu);
/// A torus-ish pipe bend / cable run segment, approximated by a bent tube.
void appendPipe(MeshBuild& m, Vec3 a, Vec3 b, f32 radius, int segments = 8, u32 color = 0xFFFFFFFFu);

void recalcNormalsFlat(MeshBuild& m);

} // namespace hv::scene
