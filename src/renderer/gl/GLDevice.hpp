#pragma once
// OpenGL 4.1 core-profile backend — the baseline renderer every Apple
// Silicon Mac can run, since Apple caps OpenGL at 4.1. Selected
// automatically when Vulkan/MoltenVK is unavailable, or explicitly via
// Settings.
#include "renderer/RenderDevice.hpp"
#include "renderer/CommandBuffer.hpp"
#include <unordered_map>
#include <vector>

namespace hv::gfx::gl {

class GLDevice final : public RenderDevice {
public:
    /// `nsOpenGLContext` is an NSOpenGLContext* already made current by the
    /// platform layer; this device only touches GL state, never AppKit.
    static std::unique_ptr<GLDevice> create(i32 pixelWidth, i32 pixelHeight);
    ~GLDevice() override;

    const DeviceInfo& deviceInfo() const override { return info_; }
    Backend backend() const override { return Backend::OpenGL; }

    void resize(i32 pixelWidth, i32 pixelHeight) override;
    void setVSync(bool enabled) override;

    TextureHandle createTexture(const TextureDesc& desc, const void* pixels) override;
    void updateTexture(TextureHandle handle, const void* pixels, i32 x, i32 y, i32 w, i32 h) override;
    void destroyTexture(TextureHandle h) override;

    MeshHandle createMesh(const MeshDesc& desc) override;
    void updateMesh(MeshHandle h, const MeshDesc& desc) override;
    void destroyMesh(MeshHandle h) override;

    ShaderHandle createShader(const ShaderDesc& desc) override;
    void destroyShader(ShaderHandle h) override;

    MaterialHandle createMaterial(const MaterialDesc& desc) override;
    void updateMaterial(MaterialHandle h, const MaterialDesc& desc) override;
    void destroyMaterial(MaterialHandle h) override;

    FramebufferHandle createFramebuffer(const FramebufferDesc& desc) override;
    void destroyFramebuffer(FramebufferHandle h) override;
    TextureHandle framebufferColorTexture(FramebufferHandle h, i32 index) override;

    CommandBuffer& begin() override;
    void submit(CommandBuffer& cmd) override;
    void present() override;
    const FrameStats& lastFrameStats() const override { return stats_; }
    u64 residentMemoryBytes() const override { return residentBytes_; }
    f64 lastGpuFrameMs() const override { return lastGpuMs_; }

private:
    GLDevice() = default;
    void queryDeviceInfo();
    void executePass(const CommandBuffer::Pass& pass);
    u32 compileProgram(const ShaderDesc& desc);

    struct GLTexture { u32 id = 0; TextureDesc desc; };
    struct GLMesh { u32 vao = 0, vbo = 0, ibo = 0, instanceVbo = 0; u32 indexCount = 0; bool dynamic = false; };
    struct GLShader { u32 program = 0; };
    struct GLMaterial { MaterialDesc desc; };
    struct GLFramebuffer { u32 fbo = 0; std::vector<TextureHandle> colors; TextureHandle depth; i32 w = 0, h = 0; };

    template <typename Tag, typename Store>
    Handle<Tag> insert(std::unordered_map<u32, Store>& map, u32& counter, Store value) {
        const u32 idx = counter++;
        map.emplace(idx, std::move(value));
        return Handle<Tag>{idx, 1};
    }

    std::unordered_map<u32, GLTexture> textures_;
    std::unordered_map<u32, GLMesh> meshes_;
    std::unordered_map<u32, GLShader> shaders_;
    std::unordered_map<u32, GLMaterial> materials_;
    std::unordered_map<u32, GLFramebuffer> framebuffers_;
    u32 nextTexture_ = 1, nextMesh_ = 1, nextShader_ = 1, nextMaterial_ = 1, nextFramebuffer_ = 1;

    /// Uniform locations resolved once at program-link time. Looking these
    /// up by name is a driver-side string hash; doing it per draw call (and
    /// per light, per frame) was hundreds of lookups a frame for no reason.
    struct SceneUniforms {
        i32 view = -1, proj = -1, model = -1, instanced = -1;
        i32 eyePos = -1, albedo = -1, metallic = -1, roughness = -1, emissive = -1;
        i32 surfaceKind = -1, texScale = -1;
        i32 ambientSky = -1, ambientGround = -1;
        i32 exposure = -1, fogDensity = -1, fogColor = -1;
        i32 saturation = -1, shadowTint = -1, highlightTint = -1;
        i32 contrast = -1, vignette = -1, viewportSize = -1;
        i32 lightCount = -1, boxCount = -1, boxMin = -1, boxMax = -1, rayTracedShadows = -1;
        i32 hasAlbedoTex = -1, albedoTex = -1;
        i32 lightPosType[16]{}, lightColorIntensity[16]{}, lightParams[16]{};
    };
    void cacheSceneUniforms();

    SceneUniforms su_{};
    u32 sceneShader_ = 0;
    DeviceInfo info_;
    FrameStats stats_;
    CommandBuffer frameCmd_;
    i32 width_ = 1, height_ = 1;
    u64 residentBytes_ = 0;
    f64 lastGpuMs_ = 0.0;
    u32 timerQueryFront_ = 0, timerQueryBack_ = 0;
    bool timerQueryPending_ = false;
};

} // namespace hv::gfx::gl
