#include "scene/SceneBuilder.hpp"
#include "scene/Primitives.hpp"
#include "core/Random.hpp"
#include <algorithm>
#include <cmath>

namespace hv::scene {
namespace {

MeshHandle upload(RenderDevice& d, MeshBuild& b, const char* name) {
    return d.createMesh(b.toDesc(name));
}

MaterialHandle makeMaterial(RenderDevice& d, Vec3 tint, f32 metallic, f32 roughness,
                            f32 emissive, SurfaceKind surface, const char* name) {
    MaterialDesc m;
    m.albedoTint = tint; m.metallic = metallic; m.roughness = roughness;
    m.emissiveStrength = emissive; m.surface = surface; m.debugName = name;
    return d.createMaterial(m);
}

} // namespace

MeshLibrary buildMeshLibrary(RenderDevice& device) {
    MeshLibrary lib;

    { MeshBuild b; appendQuadXZ(b, {0,0,0}, sim::kCellWidth * sim::kMaxMergedWidth, sim::kRoomDepth, {6,3});
      lib.roomShellFloor = upload(device, b, "room_floor"); }
    { MeshBuild b; appendBox(b, {0, sim::kFloorHeight * 0.5f, -sim::kRoomDepth * 0.5f},
                             {sim::kCellWidth * sim::kMaxMergedWidth * 0.5f, sim::kFloorHeight * 0.5f, 0.15f}, {8,2});
      lib.roomShellWallSeg = upload(device, b, "room_wall"); }
    { MeshBuild b; appendQuadXZ(b, {0,0,0}, sim::kCellWidth, sim::kRoomDepth * 3.0f, {1,3});
      lib.corridorFloor = upload(device, b, "corridor_floor"); }
    { MeshBuild b; appendPipe(b, {-1,2,0}, {1,2,0}, 0.12f, 10); lib.pipeSegment = upload(device, b, "pipe"); }
    { MeshBuild b; appendCylinder(b, {0,0,0}, 1.1f, 2.0f, 16); lib.generatorDrum = upload(device, b, "generator"); }
    { MeshBuild b; appendCylinder(b, {0,0,0}, 0.9f, 2.6f, 14); lib.waterTank = upload(device, b, "watertank"); }
    { MeshBuild b; appendBox(b, {0,1.0f,0}, {1.4f, 1.0f, 0.4f}, {2,2}); lib.plantRack = upload(device, b, "plantrack"); }
    { MeshBuild b; appendBox(b, {0,0.35f,0}, {0.9f,0.35f,1.9f}); appendBox(b, {0,1.15f,0}, {0.9f,0.35f,1.9f});
      lib.bunkBed = upload(device, b, "bunk"); }
    { MeshBuild b; appendBox(b, {0,0.45f,0}, {1.2f,0.45f,0.6f}); lib.workbench = upload(device, b, "workbench"); }
    { MeshBuild b; for (int i=0;i<3;++i) appendBox(b, {0, 0.35f + static_cast<f32>(i)*0.7f, 0}, {0.55f,0.35f,0.55f});
      lib.crateStack = upload(device, b, "crates"); }
    { MeshBuild b; appendBox(b, {0,0.5f,0}, {0.8f,0.5f,0.4f}); appendBox(b, {0,1.0f,-0.15f}, {0.7f,0.35f,0.08f});
      lib.consoleDesk = upload(device, b, "console"); }
    { MeshBuild b; appendBox(b, {0, sim::kFloorHeight*0.5f, 0}, {sim::kCellWidth*0.42f, sim::kFloorHeight*0.5f, sim::kRoomDepth*0.42f});
      lib.elevatorCar = upload(device, b, "elevator"); }
    { MeshBuild b; appendBox(b, {0, sim::kFloorHeight*0.4f, 0}, {sim::kCellWidth*0.45f, sim::kFloorHeight*0.4f, 0.08f});
      lib.doorSlab = upload(device, b, "door"); }
    { MeshBuild b; appendCapsule(b, {0,0,0}, 0.28f, 1.65f, 10); lib.residentBody = upload(device, b, "resident"); }
    { MeshBuild b; appendCapsule(b, {0,0,0}, 0.09f, 0.6f, 6); lib.residentLimb = upload(device, b, "limb"); }
    { MeshBuild b; appendCylinder(b, {0,0,0}, 0.08f, 0.3f, 8); appendBox(b, {0,0.32f,0}, {0.18f,0.05f,0.18f});
      lib.lampFixture = upload(device, b, "lamp"); }
    { MeshBuild b; appendBox(b, {0,0,0}, {0.5f,0.5f,0.5f}); lib.unitCube = upload(device, b, "unit_cube"); }

    // Each material names the procedural texture the fragment shader
    // synthesizes for it — there are no texture files anywhere in the build.
    lib.concrete       = makeMaterial(device, colorFromHex(0x6E6A62), 0.02f, 0.92f, 0.0f, SurfaceKind::Concrete, "concrete");
    lib.rustedMetal    = makeMaterial(device, colorFromHex(0x7A4A2E), 0.55f, 0.65f, 0.0f, SurfaceKind::RustedMetal, "rusted_metal");
    lib.paintedMetal   = makeMaterial(device, colorFromHex(0x3C4A42), 0.35f, 0.5f, 0.0f, SurfaceKind::PaintedMetal, "painted_metal");
    lib.glassPanel     = makeMaterial(device, colorFromHex(0x5E7C84), 0.1f, 0.15f, 0.0f, SurfaceKind::Glass, "glass");
    lib.machineHousing = makeMaterial(device, colorFromHex(0x53483C), 0.5f, 0.55f, 0.0f, SurfaceKind::BrushedMetal, "machine_housing");
    lib.fabricDorm     = makeMaterial(device, colorFromHex(0x4E5A44), 0.0f, 0.85f, 0.0f, SurfaceKind::Fabric, "fabric");
    lib.emissivePanel  = makeMaterial(device, colorFromHex(0xE0A458), 0.0f, 0.4f, 1.6f, SurfaceKind::Emissive, "emissive_panel");

    static const u32 skinTones[6] = {0xC98E63, 0xE8B98A, 0x8D5A3C, 0x6A4A33, 0xF2CBA0, 0x8A5C3E};
    for (int i = 0; i < 6; ++i)
        lib.residentSkin[i] = makeMaterial(device, colorFromHex(skinTones[i]), 0.0f, 0.7f, 0.0f, SurfaceKind::Skin, "skin");
    static const u32 outfitTones[4] = {0x4A483F, 0x36453A, 0x453434, 0x333D45};
    for (int i = 0; i < 4; ++i)
        lib.residentOutfit[i] = makeMaterial(device, colorFromHex(outfitTones[i]), 0.05f, 0.75f, 0.0f, SurfaceKind::Fabric, "outfit");

    return lib;
}

SceneRenderer::SceneRenderer(RenderDevice& device) : device_(device), meshes_(buildMeshLibrary(device)) {}

namespace {

MeshHandle meshForRoom(const MeshLibrary& lib, sim::RoomType t) {
    using sim::RoomType;
    switch (t) {
        case RoomType::Generator:     return lib.generatorDrum;
        case RoomType::WaterPlant:    return lib.waterTank;
        case RoomType::Hydroponics:   return lib.plantRack;
        case RoomType::Dormitory:     return lib.bunkBed;
        case RoomType::Workshop:      return lib.workbench;
        case RoomType::Storage:       return lib.crateStack;
        case RoomType::Armory:        return lib.crateStack;
        case RoomType::Command:       return lib.consoleDesk;
        case RoomType::Laboratory:    return lib.consoleDesk;
        case RoomType::Research:      return lib.consoleDesk;
        case RoomType::Communications:return lib.consoleDesk;
        case RoomType::Security:      return lib.workbench;
        case RoomType::Elevator:      return lib.elevatorCar;
        default: return lib.crateStack;
    }
}

Vec3 tintForRoom(sim::RoomType t) {
    using sim::RoomType;
    switch (t) {
        case RoomType::Generator: return colorFromHex(0xC97B3B);
        case RoomType::WaterPlant: return colorFromHex(0x5C93A6);
        case RoomType::Hydroponics: return colorFromHex(0x6F9950);
        default: return Vec3{1,1,1};
    }
}

} // namespace

void SceneRenderer::syncShelter(const sim::Shelter& shelter, f32 simTime) {
    roomInstances_.clear();
    for (const sim::Room& room : shelter.rooms()) {
        RoomInstance inst;
        inst.type = room.type;
        inst.bounds = room.bounds();
        inst.broken = room.broken;
        inst.fire = room.fire;
        inst.anim = room.animPhase;
        const Vec3 c = room.worldCenter();
        const f32 bob = room.def().function == sim::RoomFunction::Produce
            ? std::sin(simTime * 1.2f + room.animPhase) * 0.03f * room.powerSatisfaction : 0.0f;
        inst.model = Mat4::translate(c + Vec3{0, 0.4f + bob, 0});
        roomInstances_.push_back(inst);
    }
}

void SceneRenderer::syncResidents(const std::vector<sim::Resident>& residents, f32 /*dt*/) {
    residentInstances_.clear();
    for (const sim::Resident& r : residents) {
        if (!r.alive() || r.expeditionId != 0) continue;
        ResidentInstance inst;
        inst.model = Mat4::translate(r.position + Vec3{0, 0.85f * r.appearance.height, 0}) *
                    Mat4::rotationY(r.facing) *
                    Mat4::scale(Vec3{r.appearance.build, r.appearance.height, r.appearance.build});
        inst.bounds = AABB{r.position - Vec3{0.4f,0,0.4f}, r.position + Vec3{0.4f, 1.9f, 0.4f}};
        inst.skinIdx = r.appearance.skinTone % 6;
        inst.outfitIdx = r.appearance.outfitTint % 4;
        residentInstances_.push_back(inst);
    }
}

void SceneRenderer::render(CommandBuffer& cmd, const Frustum& frustum, const Mat4& view,
                           const Mat4& proj, const Vec3& eye, f32 dayNightT,
                           i32 rayTracingLevel) {
    RenderPassDesc pass;
    // glClear writes these values straight to the framebuffer — no shader,
    // no gamma pass — so they're picked directly as final pixel values, not
    // as linear light to be tonemapped. Kept low since this is bedrock
    // beyond the dug-out rooms, not sky, but never all the way to zero so
    // it stays visibly distinct from unlit geometry.
    pass.clearColorValue = Vec4{lerpf(0.04f, 0.08f, dayNightT), lerpf(0.045f, 0.09f, dayNightT),
                                lerpf(0.06f, 0.11f, dayNightT), 1.0f};
    cmd.beginPass(pass);
    cmd.setCamera(view, proj, eye);
    cmd.setLights(lighting_.nearest(eye, LightingSystem::kMaxLights));

    // The shadow-ray "box soup": one coarse box per room, nearest to the
    // camera first, since the shader only tests a bounded number of them.
    if (rayTracingLevel > 0) {
        scratchOccluders_.clear();
        for (const RoomInstance& r : roomInstances_) scratchOccluders_.push_back(r.bounds);
        // Only the closest boxes are uploaded, so partial_sort is enough.
        const size_t keep = std::min<size_t>(kMaxOccluderBoxes, scratchOccluders_.size());
        auto byDistance = [&](const AABB& a, const AABB& b) {
            return distanceSq(a.center(), eye) < distanceSq(b.center(), eye);
        };
        if (keep < scratchOccluders_.size())
            std::partial_sort(scratchOccluders_.begin(),
                              scratchOccluders_.begin() + static_cast<long>(keep),
                              scratchOccluders_.end(), byDistance);
        else
            std::sort(scratchOccluders_.begin(), scratchOccluders_.end(), byDistance);
        scratchOccluders_.resize(keep);
        cmd.setOccluders(scratchOccluders_);
    }
    cmd.setRayTracingLevel(rayTracingLevel);

    // Floors: one flat slab per room, always drawn (cheap, establishes footing).
    scratchFloors_.clear();
    for (const RoomInstance& r : roomInstances_) {
        if (!frustum.intersects(r.bounds)) continue;
        InstanceData id; id.model = Mat4::translate(r.bounds.center() * Vec3{1,0,1} + Vec3{0, r.bounds.min.y, 0});
        id.colorTint = Vec4{1,1,1,1};
        scratchFloors_.push_back(id);
    }
    if (!scratchFloors_.empty()) cmd.drawInstanced(meshes_.roomShellFloor, meshes_.concrete, scratchFloors_);

    // Machinery / furniture, batched per room-type mesh (generator drums,
    // water tanks, plant racks, bunks, ...) so each room actually shows the
    // fixture that matches what it does, not one generic crate everywhere.
    // A small flat vector keyed by mesh index beats a hash map here: there
    // are only a handful of distinct room meshes, and this reuses its
    // storage across frames instead of rebuilding buckets every frame.
    for (auto& bucket : scratchByMesh_) bucket.second.clear();
    for (const RoomInstance& r : roomInstances_) {
        if (!frustum.intersects(r.bounds)) continue;
        InstanceData id; id.model = r.model;
        const Vec3 tint = tintForRoom(r.type);
        const f32 dim = r.broken ? 0.4f : (1.0f - r.fire * 0.3f);
        id.colorTint = Vec4{tint.x * dim, tint.y * dim, tint.z * dim, 1};
        id.customA = r.fire > 0.05f ? 1.0f : 0.0f;
        const u32 meshIndex = meshForRoom(meshes_, r.type).index;
        auto it = std::find_if(scratchByMesh_.begin(), scratchByMesh_.end(),
                               [meshIndex](const auto& b) { return b.first == meshIndex; });
        if (it == scratchByMesh_.end()) {
            scratchByMesh_.emplace_back(meshIndex, std::vector<InstanceData>{});
            it = scratchByMesh_.end() - 1;
        }
        it->second.push_back(id);
    }
    for (const auto& [meshIndex, instances] : scratchByMesh_) {
        if (instances.empty()) continue;
        cmd.drawInstanced(MeshHandle{meshIndex, 1}, meshes_.machineHousing, instances);
    }

    // Residents, instanced per skin/outfit bucket to keep draw calls low even
    // with hundreds on screen.
    for (u8 skin = 0; skin < 6; ++skin) {
        scratchBatch_.clear();
        for (const ResidentInstance& r : residentInstances_) {
            if (r.skinIdx != skin || !frustum.intersects(r.bounds)) continue;
            InstanceData id; id.model = r.model; id.colorTint = Vec4{1,1,1,1};
            scratchBatch_.push_back(id);
        }
        if (!scratchBatch_.empty())
            cmd.drawInstanced(meshes_.residentBody, meshes_.residentSkin[skin], scratchBatch_);
    }

    cmd.endPass();
}

} // namespace hv::scene
