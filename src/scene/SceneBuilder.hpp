#pragma once
// Bridges sim::World to the renderer: turns the shelter grid, rooms,
// residents and effects into a MeshLibrary of procedural geometry plus a
// per-frame list of instanced draws and lights. This is what makes the game
// a genuine 3D scene rather than a 2D board with a camera bolted on.
#include "renderer/RenderDevice.hpp"
#include "renderer/CommandBuffer.hpp"
#include "renderer/LightingSystem.hpp"
#include "sim/World.hpp"
#include <unordered_map>

namespace hv::scene {

using namespace hv::gfx;

/// One procedural mesh + material per archetype (room shell, generator drum,
/// resident capsule, ...). Built once; reused every frame via instancing.
struct MeshLibrary {
    MeshHandle roomShellFloor;      ///< flat floor slab
    MeshHandle roomShellWallSeg;    ///< a wall segment with a door cut-out option
    MeshHandle corridorFloor;
    MeshHandle pipeSegment;
    MeshHandle generatorDrum;
    MeshHandle waterTank;
    MeshHandle plantRack;
    MeshHandle bunkBed;
    MeshHandle workbench;
    MeshHandle crateStack;
    MeshHandle consoleDesk;
    MeshHandle elevatorCar;
    MeshHandle doorSlab;
    MeshHandle residentBody;        ///< capsule torso+head silhouette
    MeshHandle residentLimb;
    MeshHandle lampFixture;
    MeshHandle unitCube;            ///< generic box, reused for placeholders/effects

    MaterialHandle concrete;
    MaterialHandle rustedMetal;
    MaterialHandle paintedMetal;
    MaterialHandle glassPanel;
    MaterialHandle machineHousing;
    MaterialHandle fabricDorm;
    MaterialHandle emissivePanel;
    MaterialHandle residentSkin[6];
    MaterialHandle residentOutfit[4];
};

MeshLibrary buildMeshLibrary(RenderDevice& device);

class SceneRenderer {
public:
    explicit SceneRenderer(RenderDevice& device);

    /// Rebuilds room instance buffers when the shelter layout changes; cheap
    /// enough to call every frame but the caller can skip it when nothing
    /// changed (a version counter would be the next optimisation).
    void syncShelter(const sim::Shelter& shelter, f32 simTime);
    void syncResidents(const std::vector<sim::Resident>& residents, f32 dt);

    /// Fills a CommandBuffer pass with every visible draw, culled against
    /// the given frustum.
    void render(CommandBuffer& cmd, const Frustum& frustum, const Mat4& view,
               const Mat4& proj, const Vec3& eye, f32 dayNightT);

    LightingSystem& lighting() { return lighting_; }
    const MeshLibrary& meshes() const { return meshes_; }

private:
    RenderDevice& device_;
    MeshLibrary meshes_;
    LightingSystem lighting_;

    struct RoomInstance { AABB bounds; sim::RoomType type; Mat4 model; f32 anim; bool broken; f32 fire; };
    std::vector<RoomInstance> roomInstances_;

    struct ResidentInstance { AABB bounds; Mat4 model; u8 skinIdx; u8 outfitIdx; };
    std::vector<ResidentInstance> residentInstances_;
};

} // namespace hv::scene
