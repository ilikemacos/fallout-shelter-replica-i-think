#pragma once
// The top-level application: owns the window, render device, audio, the
// simulation World, the scene renderer, the UI, and the save manager, and
// drives the fixed-step simulation + variable-step render loop.
#include "platform/Window.hpp"
#include "platform/macos/MacSystemInfo.hpp"
#include "renderer/RenderDevice.hpp"
#include "renderer/Camera.hpp"
#include "scene/SceneBuilder.hpp"
#include "audio/AudioSystem.hpp"
#include "ui/UIRenderer.hpp"
#include "debug/DebugPanel.hpp"
#include "sim/World.hpp"
#include "save/SaveManager.hpp"
#include "ai/ResidentAI.hpp"
#include "core/Settings.hpp"
#include "core/Time.hpp"
#include "core/Profiler.hpp"
#include <memory>

namespace hv::game {

enum class BuildMode : u8 { None = 0, Placing };

class App {
public:
    App();
    ~App();
    /// Runs until the window closes. Blocks for the lifetime of the app.
    int run();

private:
    void loadOrCreateSave();
    void handleInput(f32 dt);
    void tickSimulation(f32 dt);
    void renderFrame(f32 dt);
    void renderHUD();
    void renderDebugPanel();
    void renderBuildMenu();
    void renderRoomInspector();
    void renderNotifications();
    void applyGraphicsSettings();
    sim::Cell pickCell() const;
    sim::RoomId pickRoom() const;

    Settings settings_;
    std::unique_ptr<platform::Window> window_;
    std::unique_ptr<gfx::RenderDevice> device_;
    std::unique_ptr<audio::AudioSystem> audio_;
    std::unique_ptr<ui::UIRenderer> ui_;
    std::unique_ptr<scene::SceneRenderer> sceneRenderer_;
    std::unique_ptr<save::SaveManager> saveManager_;

    sim::World world_;
    ai::ResidentAI residentAI_;
    gfx::Camera camera_;
    platform::SystemInfo systemInfo_;

    StepClock simClock_{1.0 / 30.0};   ///< sim runs at 30Hz regardless of render fps
    RollingAverage<120> fpsHistory_;
    debug::DebugPanelData debugData_;
    Profiler profiler_;

    BuildMode buildMode_ = BuildMode::None;
    sim::RoomType selectedRoomType_ = sim::RoomType::Corridor;
    sim::RoomId selectedRoom_ = sim::kNoRoom;
    sim::ResidentId selectedResident_ = sim::kNoResident;
    f32 autosaveTimer_ = 0.0f;
    bool wasPausedByFocusLoss_ = false;
    f64 lastFrameTime_ = 0.0;
};

} // namespace hv::game
