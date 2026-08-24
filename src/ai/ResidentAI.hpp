#pragma once
// Distance/priority-optimized behaviour for resident bodies: pick an
// activity, steer to the right point in the 3D shelter, and update posture.
// Runs every simulation tick for everyone; only residents near the camera
// get the finer per-frame steering update (distance-based LOD for AI cost).
#include "sim/World.hpp"

namespace hv::ai {

using namespace hv::sim;

class ResidentAI {
public:
    /// Full-rate update: decides activity/destination. Cheap, runs for all.
    void tickDecision(World& world, Resident& r, f32 dt);
    /// Movement integration; callers give more residents higher-fidelity dt
    /// (e.g. skip frames for residents far from the camera).
    void tickMovement(World& world, Resident& r, f32 dt);

private:
    Vec3 destinationFor(World& world, Resident& r) const;
};

} // namespace hv::ai
