#pragma once
// A backend-neutral, frame-scoped list of draw work. Populated by the scene
// renderer and the UI, then handed to RenderDevice::submit.
#include "renderer/RenderTypes.hpp"
#include <vector>

namespace hv::gfx {

struct DrawItem {
    MeshHandle mesh;
    MaterialHandle material;
    Mat4 model = Mat4::identity();
    Vec4 tint{1, 1, 1, 1};
    f32  customA = 0.0f;
    i32  instanceStart = -1;   ///< index into the owning pass's instance buffer, -1 = none
    i32  instanceCount = 0;
    f32  sortDepth = 0.0f;     ///< camera-space depth, used to sort transparents back-to-front
};

struct RenderPassDesc {
    FramebufferHandle target;   ///< default handle (index 0, generation 0) = the swapchain
    bool clearColor = true;
    Vec4 clearColorValue{0, 0, 0, 1};
    bool clearDepth = true;
    const char* debugLabel = "";
};

/// One logical pass: a target, a camera, some lights, some draws.
class CommandBuffer {
public:
    void beginPass(const RenderPassDesc& desc) { Pass p; p.desc = desc; passes_.push_back(std::move(p)); }
    void setCamera(const Mat4& view, const Mat4& proj, const Vec3& eyePos) {
        if (passes_.empty()) return;
        passes_.back().view = view;
        passes_.back().proj = proj;
        passes_.back().eye = eyePos;
    }
    void setLights(std::vector<Light> lights) {
        if (!passes_.empty()) passes_.back().lights = std::move(lights);
    }
    void draw(const DrawItem& item) { if (!passes_.empty()) passes_.back().items.push_back(item); }
    void drawInstanced(MeshHandle mesh, MaterialHandle material, std::vector<InstanceData> instances) {
        if (passes_.empty()) return;
        Pass& p = passes_.back();
        DrawItem item;
        item.mesh = mesh; item.material = material;
        item.instanceStart = static_cast<i32>(p.instances.size());
        item.instanceCount = static_cast<i32>(instances.size());
        p.instances.insert(p.instances.end(), instances.begin(), instances.end());
        p.items.push_back(item);
    }
    void endPass() {}

    struct Pass {
        RenderPassDesc desc;
        std::vector<DrawItem> items;
        std::vector<InstanceData> instances;
        std::vector<Light> lights;
        Mat4 view = Mat4::identity();
        Mat4 proj = Mat4::identity();
        Vec3 eye;
    };
    const std::vector<Pass>& passes() const { return passes_; }
    void reset() { passes_.clear(); }

private:
    std::vector<Pass> passes_;
};

} // namespace hv::gfx
