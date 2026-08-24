#pragma once
// State for the in-game debug/renderer-info overlay (toggled with F3).
// Populated fresh every frame from real device/window/simulation state —
// nothing here is ever a placeholder value.
#include "renderer/RenderTypes.hpp"
#include "core/Time.hpp"
#include <string>

namespace hv::debug {

struct DebugPanelData {
    bool visible = false;

    gfx::Backend backend = gfx::Backend::None;
    std::string gpuName;
    std::string apiVersion;

    f64 fps = 0.0;
    f64 frameTimeMs = 0.0;
    f64 cpuFrameTimeMs = 0.0;
    f64 gpuFrameTimeMs = 0.0;

    u32 drawCalls = 0;
    u32 instancedDrawCalls = 0;
    u64 triangles = 0;

    i32 pixelWidth = 0, pixelHeight = 0;
    f32 renderScale = 1.0f;
    u64 residentMemoryBytes = 0;

    i32 population = 0;
    i32 residentEntities = 0;
    i32 activeEmergencies = 0;
    i32 activeExpeditions = 0;
    f32 simGameDay = 1.0f;

    RollingAverage<120> frameHistory;
};

} // namespace hv::debug
