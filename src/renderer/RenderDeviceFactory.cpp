// Backend selection: Vulkan/MoltenVK would be tried first here once
// src/renderer/vk/ lands (CMake already detects headers and bakes SPIR-V
// for it — see HAVEN_ENABLE_VULKAN). Until then this always uses the
// OpenGL 4.1 core backend, which every Apple Silicon Mac supports. Never
// fabricates a device — if it can't actually initialize, this returns
// nullptr and the caller fails loudly instead of pretending to render.
#include "renderer/RenderDevice.hpp"
#include "renderer/gl/GLDevice.hpp"
#include "core/Log.hpp"

namespace hv::gfx {

std::unique_ptr<RenderDevice> RenderDevice::create(BackendKind requested, void* nativeWindow,
                                                    i32 pixelWidth, i32 pixelHeight) {
    (void)nativeWindow;
    if (requested == BackendKind::Vulkan)
        HV_WARN("Renderer: Vulkan was requested but this build has no Vulkan backend yet — using OpenGL");

    if (auto dev = gl::GLDevice::create(pixelWidth, pixelHeight)) {
        HV_INFO("Renderer: OpenGL 4.1 core selected");
        return dev;
    }
    HV_ERROR("Renderer: no backend could initialize");
    return nullptr;
}

} // namespace hv::gfx
