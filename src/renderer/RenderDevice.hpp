#pragma once
// The renderer abstraction every backend (OpenGL, Vulkan) implements.
// Nothing in scene/, ui/ or game/ includes gl.h or vulkan.h — everything
// goes through this interface, selected once at startup by RenderDevice::create.
#include "renderer/RenderTypes.hpp"
#include "core/Settings.hpp"
#include <memory>
#include <string>

namespace hv::gfx {

class CommandBuffer;

/// Owns the GPU resources and knows how to draw a frame. One instance per
/// running game; created by the platform window once a context exists.
class RenderDevice {
public:
    virtual ~RenderDevice() = default;

    /// Tries the requested backend; on Auto, prefers Vulkan-via-MoltenVK
    /// when present and falls back to OpenGL 4.1 core otherwise. Returns
    /// nullptr (never a fake device) when nothing usable is available.
    static std::unique_ptr<RenderDevice> create(BackendKind requested, void* nativeWindow,
                                                i32 pixelWidth, i32 pixelHeight);

    virtual const DeviceInfo& deviceInfo() const = 0;
    virtual Backend backend() const = 0;

    virtual void resize(i32 pixelWidth, i32 pixelHeight) = 0;
    virtual void setVSync(bool enabled) = 0;

    // ---- resource creation --------------------------------------------------
    virtual TextureHandle createTexture(const TextureDesc& desc, const void* pixels) = 0;
    virtual void updateTexture(TextureHandle handle, const void* pixels, i32 x, i32 y, i32 w, i32 h) = 0;
    virtual void destroyTexture(TextureHandle h) = 0;

    virtual MeshHandle createMesh(const MeshDesc& desc) = 0;
    virtual void updateMesh(MeshHandle h, const MeshDesc& desc) = 0;
    virtual void destroyMesh(MeshHandle h) = 0;

    virtual ShaderHandle createShader(const ShaderDesc& desc) = 0;
    virtual void destroyShader(ShaderHandle h) = 0;

    virtual MaterialHandle createMaterial(const MaterialDesc& desc) = 0;
    virtual void updateMaterial(MaterialHandle h, const MaterialDesc& desc) = 0;
    virtual void destroyMaterial(MaterialHandle h) = 0;

    virtual FramebufferHandle createFramebuffer(const FramebufferDesc& desc) = 0;
    virtual void destroyFramebuffer(FramebufferHandle h) = 0;
    virtual TextureHandle framebufferColorTexture(FramebufferHandle h, i32 index = 0) = 0;

    // ---- per-frame -----------------------------------------------------------
    virtual CommandBuffer& begin() = 0;
    virtual void submit(CommandBuffer& cmd) = 0;
    virtual void present() = 0;
    virtual const FrameStats& lastFrameStats() const = 0;

    /// Approximate GPU-resident bytes and, when the platform exposes it, the
    /// last measured GPU frame time in milliseconds (0 when unavailable —
    /// never fabricated).
    virtual u64 residentMemoryBytes() const = 0;
    virtual f64 lastGpuFrameMs() const = 0;
};

} // namespace hv::gfx
