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
    void beginPass(const RenderPassDesc& desc) {
        if (!retired_.empty()) {
            passes_.push_back(std::move(retired_.back()));
            retired_.pop_back();
            passes_.back().desc = desc;
        } else {
            Pass p; p.desc = desc; passes_.push_back(std::move(p));
        }
    }
    void setCamera(const Mat4& view, const Mat4& proj, const Vec3& eyePos) {
        if (passes_.empty()) return;
        passes_.back().view = view;
        passes_.back().proj = proj;
        passes_.back().eye = eyePos;
    }
    void setLights(const std::vector<Light>& lights) {
        if (!passes_.empty()) passes_.back().lights = lights;
    }
    /// Coarse world boxes the ray-traced shadow pass tests against — the
    /// "box soup". Kept deliberately small (rooms, not triangles) so a
    /// per-pixel slab test over all of them stays cheap.
    void setOccluders(const std::vector<AABB>& boxes) {
        if (!passes_.empty()) passes_.back().occluders = boxes;
    }
    /// 0 = off, 1 = very light (sun only), 2 = low (sun + nearest fixtures).
    void setRayTracingLevel(i32 level) {
        if (!passes_.empty()) passes_.back().rayTracingLevel = level;
    }
    void draw(const DrawItem& item) { if (!passes_.empty()) passes_.back().items.push_back(item); }
    void drawInstanced(MeshHandle mesh, MaterialHandle material, const std::vector<InstanceData>& instances) {
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
        std::vector<AABB> occluders;
        i32 rayTracingLevel = 0;
        Mat4 view = Mat4::identity();
        Mat4 proj = Mat4::identity();
        Vec3 eye;
    };
    const std::vector<Pass>& passes() const { return passes_; }
    /// Clears the recorded work but keeps every buffer's capacity, so a
    /// steady-state frame records its draws without touching the heap.
    void reset() {
        for (Pass& p : passes_) {
            p.items.clear();
            p.instances.clear();
            p.lights.clear();
            p.occluders.clear();
        }
        retired_.insert(retired_.end(), std::make_move_iterator(passes_.begin()),
                        std::make_move_iterator(passes_.end()));
        passes_.clear();
    }

private:
    std::vector<Pass> passes_;
    /// Emptied passes kept around so their buffers can be reused next frame.
    std::vector<Pass> retired_;
};

} // namespace hv::gfx
