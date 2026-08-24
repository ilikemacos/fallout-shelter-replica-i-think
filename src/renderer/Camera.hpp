#pragma once
// Polished three-quarter/isometric-style camera: orbits a focus point with
// smooth damping, WASD/arrow pan, mouse-drag rotate, scroll zoom, and a
// focus-resident/focus-room mode that gently tracks a moving target.
#include "core/Math.hpp"
#include "core/Time.hpp"

namespace hv::gfx {

struct CameraLimits {
    f32 minDistance = 4.0f;
    f32 maxDistance = 60.0f;
    f32 minPitch = 18.0f * kDeg2Rad;
    f32 maxPitch = 75.0f * kDeg2Rad;
    Vec3 boundsMin{-90.0f, -60.0f, -40.0f};
    Vec3 boundsMax{90.0f, 6.0f, 40.0f};
};

class Camera {
public:
    void setLimits(const CameraLimits& l) { limits_ = l; }

    /// Immediate pan in world-space XZ (screen-relative, camera yaw applied).
    void pan(Vec2 screenDelta, f32 dt) {
        const f32 s = distance_ * 0.0016f * panSpeed_;
        const Vec3 fwd{-std::sin(yaw_), 0, -std::cos(yaw_)};
        const Vec3 right{std::cos(yaw_), 0, -std::sin(yaw_)};
        targetFocus_ += (right * screenDelta.x + fwd * -screenDelta.y) * s;
        targetFocus_ = clampToBounds(targetFocus_);
        followTarget_ = false;
        (void)dt;
    }
    void orbit(Vec2 screenDelta) {
        targetYaw_ -= screenDelta.x * 0.0055f * rotateSpeed_;
        targetPitch_ = clampf(targetPitch_ - screenDelta.y * 0.0035f * rotateSpeed_,
                              limits_.minPitch, limits_.maxPitch);
    }
    void zoom(f32 delta) {
        targetDistance_ = clampf(targetDistance_ - delta * targetDistance_ * 0.12f * zoomSpeed_,
                                 limits_.minDistance, limits_.maxDistance);
    }
    void focusPoint(const Vec3& worldPos) {
        targetFocus_ = clampToBounds(worldPos);
        followTarget_ = false;
    }
    /// Smoothly tracks a moving world position (a resident, a room centre)
    /// every frame until the player pans or orbits away from it.
    void follow(const Vec3& worldPos) {
        followTarget_ = true;
        followPos_ = worldPos;
    }
    void stopFollowing() { followTarget_ = false; }
    bool isFollowing() const { return followTarget_; }

    void update(f32 dt) {
        if (followTarget_) targetFocus_ = clampToBounds(followPos_);
        focus_    = lerp(focus_, targetFocus_, 1.0f - std::exp(-dampRate_ * dt));
        distance_ = damp(distance_, targetDistance_, dampRate_, dt);
        yaw_      = damp(yaw_, targetYaw_, dampRate_, dt);
        pitch_    = damp(pitch_, targetPitch_, dampRate_, dt);
        rebuild();
    }

    const Mat4& view() const { return view_; }
    const Mat4& projection() const { return proj_; }
    Vec3 eyePosition() const { return eye_; }
    Vec3 focusPosition() const { return focus_; }
    Frustum frustum() const { return Frustum::fromViewProj(proj_ * view_); }

    void setAspect(f32 aspect) { aspect_ = aspect; }
    void setFovDegrees(f32 fov) { fovDeg_ = fov; }
    void setSpeeds(f32 pan, f32 rotate, f32 zoomS) { panSpeed_ = pan; rotateSpeed_ = rotate; zoomSpeed_ = zoomS; }

    /// Builds a world-space ray for mouse picking, given normalized device
    /// coordinates in [-1,1].
    Ray screenRay(f32 ndcX, f32 ndcY) const {
        const Mat4 invVP = (proj_ * view_).inverse();
        const Vec4 nearP = invVP * Vec4{ndcX, ndcY, -1.0f, 1.0f};
        const Vec4 farP  = invVP * Vec4{ndcX, ndcY,  1.0f, 1.0f};
        const Vec3 a = (nearP.xyz()) * (1.0f / nearP.w);
        const Vec3 b = (farP.xyz()) * (1.0f / farP.w);
        Ray r; r.origin = a; r.dir = normalize(b - a);
        return r;
    }

private:
    Vec3 clampToBounds(const Vec3& p) const { return maxv(limits_.boundsMin, minv(limits_.boundsMax, p)); }

    void rebuild() {
        const f32 cp = std::cos(pitch_), sp = std::sin(pitch_);
        const f32 cy = std::cos(yaw_), sy = std::sin(yaw_);
        const Vec3 offset{ distance_ * cp * sy, distance_ * sp, distance_ * cp * cy };
        eye_ = focus_ + offset;
        view_ = Mat4::lookAt(eye_, focus_, Vec3{0, 1, 0});
        proj_ = Mat4::perspective(fovDeg_ * kDeg2Rad, aspect_, 0.1f, 400.0f);
    }

    CameraLimits limits_;
    Vec3 focus_{0, -4, 0}, targetFocus_{0, -4, 0};
    f32 distance_ = 22.0f, targetDistance_ = 22.0f;
    f32 yaw_ = 0.6f, targetYaw_ = 0.6f;
    f32 pitch_ = 0.75f, targetPitch_ = 0.75f;
    f32 fovDeg_ = 45.0f;
    f32 aspect_ = 16.0f / 9.0f;
    f32 dampRate_ = 9.0f;
    f32 panSpeed_ = 1.0f, rotateSpeed_ = 1.0f, zoomSpeed_ = 1.0f;
    bool followTarget_ = false;
    Vec3 followPos_;
    Vec3 eye_;
    Mat4 view_ = Mat4::identity();
    Mat4 proj_ = Mat4::identity();
};

} // namespace hv::gfx
