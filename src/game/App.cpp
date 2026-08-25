#include "game/App.hpp"
#include "core/Log.hpp"
#include "core/Paths.hpp"
#include <algorithm>
#include <cstdio>

namespace hv::game {
using namespace hv;
using namespace hv::sim;

namespace {
std::string fmtNum(f32 v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d", static_cast<int>(v));
    return buf;
}
u32 severityColor(NotifySeverity s) {
    switch (s) {
        case NotifySeverity::Good: return ui::palette::kGood;
        case NotifySeverity::Warning: return ui::palette::kWarning;
        case NotifySeverity::Critical: return ui::palette::kCritical;
        default: return ui::palette::kTextPrimary;
    }
}
} // namespace

App::App() {
    systemInfo_ = platform::querySystemInfo();
    HV_INFO("Haven starting on %s (%s), macOS %s", systemInfo_.cpuBrand.c_str(),
           systemInfo_.isAppleSilicon ? "Apple Silicon" : "NOT Apple Silicon",
           systemInfo_.macOSVersion.c_str());
    if (!systemInfo_.isAppleSilicon)
        HV_WARN("Haven is built for Apple Silicon; this machine reports otherwise.");

    paths::ensureDirectory(paths::logsDir());
    log::init(paths::logsDir() + "/haven.log");

    settings_.load(paths::configFile());
    window_ = platform::Window::create("Haven: Deep Shelter", settings_.graphics);
    const platform::DisplayMetrics dm = window_->metrics();

    device_ = gfx::RenderDevice::create(settings_.graphics.backend, window_->nativeViewHandle(),
                                        dm.pixelWidth, dm.pixelHeight);
    if (!device_) {
        HV_ERROR("No usable renderer — exiting.");
        return;
    }
    window_->setVSync(settings_.graphics.vsync);

    sceneRenderer_ = std::make_unique<scene::SceneRenderer>(*device_);
    ui_ = ui::UIRenderer::create();
    audio_ = audio::AudioSystem::create();
    audio_->setSettings(settings_.audio);
    audio_->setAmbience(audio::Ambience::ShelterHum, 0.1f);

    saveManager_ = std::make_unique<save::SaveManager>(paths::savesDir());
    camera_.setAspect(static_cast<f32>(dm.pixelWidth) / static_cast<f32>(std::max(1, dm.pixelHeight)));
    camera_.setSpeeds(settings_.gameplay.cameraSpeed, settings_.gameplay.cameraSpeed,
                      settings_.gameplay.zoomSpeed);

    loadOrCreateSave();
}

App::~App() {
    if (saveManager_) saveManager_->save(save::SaveManager::kAutosaveSlot, world_);
    settings_.save(paths::configFile());
    log::shutdown();
}

void App::loadOrCreateSave() {
    if (saveManager_->loadAutosave(world_)) {
        HV_INFO("Loaded autosave: day %d, %d residents", world_.day(), world_.population());
        return;
    }
    HV_INFO("No save found — starting a new shelter.");
    world_.newGame(static_cast<u64>(nowSeconds() * 1000.0));
}

int App::run() {
    if (!device_) return 1;
    f64 previous = nowSeconds();
    while (!window_->shouldClose()) {
        window_->pumpEvents();
        const f64 now = nowSeconds();
        f32 dt = static_cast<f32>(now - previous);
        previous = now;
        dt = std::min(dt, 0.1f);   // guard against a debugger pause or window drag stall

        if (settings_.gameplay.pauseOnFocusLoss) {
            if (!window_->isFocused() && !world_.paused()) { world_.setPaused(true); wasPausedByFocusLoss_ = true; }
            else if (window_->isFocused() && wasPausedByFocusLoss_) { world_.setPaused(false); wasPausedByFocusLoss_ = false; }
        }

        handleInput(dt);

        profiler_.beginFrame();
        const int steps = simClock_.advance(dt);
        for (int i = 0; i < steps; ++i) tickSimulation(simClock_.stepF());

        camera_.update(dt);
        audio_->update(dt);
        renderFrame(dt);
        profiler_.endFrame();

        fpsHistory_.push(dt > 0.0 ? 1.0 / dt : 0.0);
        lastFrameTime_ = dt;
    }
    return 0;
}

void App::tickSimulation(f32 dt) {
    if (!world_.paused()) {
        for (Resident& r : world_.residents()) residentAI_.tickDecision(world_, r, dt);
        world_.tick(dt);
        for (Resident& r : world_.residents()) residentAI_.tickMovement(world_, r, dt);
    }

    if (settings_.gameplay.autosaveEnabled) {
        autosaveTimer_ += dt;
        if (autosaveTimer_ > static_cast<f32>(settings_.gameplay.autosaveMinutes) * 60.0f) {
            autosaveTimer_ = 0.0f;
            saveManager_->autosave(world_);
        }
    }
}

Cell App::pickCell() const {
    const input::InputState& in = window_->input();
    const platform::DisplayMetrics dm = window_->metrics();
    const f32 ndcX = (in.mousePos.x / static_cast<f32>(dm.pointWidth)) * 2.0f - 1.0f;
    const f32 ndcY = 1.0f - (in.mousePos.y / static_cast<f32>(dm.pointHeight)) * 2.0f;
    const Ray ray = camera_.screenRay(ndcX, ndcY);
    // Intersect against each floor plane and keep the closest hit — cheap
    // since the shelter is at most kMaxFloors planes.
    Cell best; f32 bestT = 1e30f; bool found = false;
    for (i32 floor = 0; floor <= world_.shelter().deepestFloor() + 1 && floor < kMaxFloors; ++floor) {
        f32 tf = 0.0f;
        if (!ray.intersectPlaneY(-static_cast<f32>(floor) * kFloorHeight, tf)) continue;
        if (tf >= bestT) continue;
        const Vec3 hit = ray.at(tf);
        Cell c = worldToCell(Vec3{hit.x, -static_cast<f32>(floor) * kFloorHeight, hit.z});
        c.floor = floor;
        if (c.inBounds()) { best = c; bestT = tf; found = true; }
    }
    return found ? best : Cell{-1, -1};
}

RoomId App::pickRoom() const {
    const Cell c = pickCell();
    if (!c.inBounds()) return kNoRoom;
    return world_.shelter().roomIdAt(c);
}

void App::handleInput(f32 dt) {
    const input::InputState& in = window_->input();
    using input::Key;
    using input::MouseButton;

    if (in.pressed(Key::F3)) settings_.showDebugPanel = !settings_.showDebugPanel;
    if (in.pressed(Key::Space)) world_.setPaused(!world_.paused());
    if (in.pressed(Key::Plus)) world_.setSpeed(std::min(8.0f, world_.speed() * 2.0f <= 0.0f ? 1.0f : world_.speed() + 1.0f));
    if (in.pressed(Key::Minus)) world_.setSpeed(std::max(0.0f, world_.speed() - 1.0f));
    if (in.pressed(Key::Escape)) { buildMode_ = BuildMode::None; selectedRoom_ = kNoRoom; }

    static const RoomType kHotbar[9] = {
        RoomType::Corridor, RoomType::Generator, RoomType::WaterPlant, RoomType::Hydroponics,
        RoomType::Storage, RoomType::Dormitory, RoomType::Cafeteria, RoomType::Workshop, RoomType::Medical
    };
    for (int i = 0; i < 9; ++i) {
        const Key k = static_cast<Key>(static_cast<int>(Key::Num1) + i);
        if (in.pressed(k)) { buildMode_ = BuildMode::Placing; selectedRoomType_ = kHotbar[i]; }
    }

    // Camera: right-drag orbits, left-drag (outside build mode) pans.
    if (in.down(MouseButton::Right)) camera_.orbit(in.mouseDelta);
    else if (in.down(MouseButton::Middle)) camera_.pan(in.mouseDelta, dt);
    if (std::fabs(in.scrollDelta) > 1e-4f) camera_.zoom(in.scrollDelta);

    f32 panX = 0, panY = 0;
    if (in.down(Key::W) || in.down(Key::Up)) panY -= 1.0f;
    if (in.down(Key::S) || in.down(Key::Down)) panY += 1.0f;
    if (in.down(Key::A) || in.down(Key::Left)) panX -= 1.0f;
    if (in.down(Key::D) || in.down(Key::Right)) panX += 1.0f;
    if (panX != 0.0f || panY != 0.0f) camera_.pan(Vec2{panX, panY} * (400.0f * dt), dt);

    if (in.pressed(MouseButton::Left)) {
        if (buildMode_ == BuildMode::Placing) {
            const Cell c = pickCell();
            if (c.inBounds()) {
                const RoomDef& def = roomDef(selectedRoomType_);
                const BuildPlan plan = world_.tryBuild(selectedRoomType_, c, def.width);
                if (plan.ok()) { audio_->playSfx(audio::Sfx::Construction); buildMode_ = BuildMode::None; }
                else audio_->playSfx(audio::Sfx::UIError);
            }
        } else {
            const RoomId r = pickRoom();
            if (r != kNoRoom) { selectedRoom_ = r; audio_->playSfx(audio::Sfx::UIClick); }
        }
    }

    if (in.pressed(Key::Tab)) {
        const std::vector<Resident>& residents = world_.residents();
        if (!residents.empty()) {
            size_t idx = 0;
            for (size_t i = 0; i < residents.size(); ++i)
                if (residents[i].id == selectedResident_) { idx = (i + 1) % residents.size(); break; }
            selectedResident_ = residents[idx].id;
            camera_.follow(residents[idx].position);
        }
    }
    if (selectedRoom_ != kNoRoom) {
        if (in.pressed(Key::U)) { if (world_.upgradeRoom(selectedRoom_)) audio_->playSfx(audio::Sfx::LevelUp); }
        if (in.pressed(Key::R)) { if (world_.rushRoom(selectedRoom_)) audio_->playSfx(audio::Sfx::Rush); }
        if (in.pressed(Key::C)) { if (world_.collectRoom(selectedRoom_) > 0.0f) audio_->playSfx(audio::Sfx::Collect); }
    }
    if (in.pressed(Key::F)) { const i32 n = world_.autoAssignAll(); if (n > 0) audio_->playSfx(audio::Sfx::UIConfirm); }

    const bool cmd = in.down(Key::LeftCmd) || in.down(Key::RightCmd);
    if (cmd && in.pressed(Key::S)) saveManager_->save(1, world_);
    if (cmd && in.pressed(Key::L)) saveManager_->load(1, world_);
}

void App::renderFrame(f32 dt) {
    const platform::DisplayMetrics dm = window_->metrics();
    device_->resize(dm.pixelWidth, dm.pixelHeight);
    camera_.setAspect(static_cast<f32>(dm.pixelWidth) / static_cast<f32>(std::max(1, dm.pixelHeight)));

    sceneRenderer_->syncShelter(world_.shelter(), world_.gameTimeSeconds());
    sceneRenderer_->syncResidents(world_.residents(), dt);

    // Warm industrial fixtures per room, plus a soft directional fill
    // standing in for the buried facility's ambient bounce light.
    scene::SceneRenderer& sr = *sceneRenderer_;
    sr.lighting().beginFrame();
    gfx::Light sun;
    sun.type = gfx::LightType::Directional;
    sun.direction = normalize(Vec3{0.3f, -1.0f, 0.2f});
    sun.color = Vec3{0.6f, 0.65f, 0.75f};
    sun.intensity = 0.4f;
    sr.lighting().addLight(sun);
    for (const Room& room : world_.shelter().rooms()) {
        if (room.buildProgress < 1.0f) continue;
        gfx::Light fixture;
        fixture.type = gfx::LightType::Point;
        fixture.position = room.worldCenter() + Vec3{0, kFloorHeight * 0.85f, 0};
        const bool emergency = room.fire > 0.05f || room.broken;
        fixture.color = emergency ? Vec3{0.9f, 0.35f, 0.25f} : Vec3{1.0f, 0.78f, 0.5f};
        fixture.intensity = gfx::flickerIntensity(world_.gameTimeSeconds(), room.broken ? 6.0f : 0.0f,
                                            room.broken ? 0.6f : 0.0f, room.id) *
                            (room.powerSatisfaction * 1.5f + 0.3f);
        fixture.range = kCellWidth * static_cast<f32>(room.width) * 0.9f;
        sr.lighting().addLight(fixture);
    }

    gfx::CommandBuffer& cmd = device_->begin();
    sr.render(cmd, camera_.frustum(), camera_.view(), camera_.projection(), camera_.eyePosition(),
             world_.daylight());
    device_->submit(cmd);

    ui_->beginFrame(dm.pixelWidth, dm.pixelHeight, dm.backingScale);
    renderHUD();
    if (settings_.showDebugPanel) renderDebugPanel();
    ui_->endFrame();

    device_->present();
    window_->swapBuffers();
}

void App::renderHUD() {
    const platform::DisplayMetrics dm = window_->metrics();
    const f32 screenW = static_cast<f32>(dm.pointWidth);

    // Resource bar across the top.
    ui_->rect({0, 0}, {screenW, 34}, ui::palette::kPanelBg);
    f32 x = 12.0f;
    static const Resource order[] = {Resource::Power, Resource::Water, Resource::Food,
                                     Resource::Materials, Resource::Medicine, Resource::Research, Resource::Scrip};
    for (Resource r : order) {
        const std::string label = std::string(resourceShortName(r)) + " " + fmtNum(world_.resources().get(r));
        ui_->text({x, 10}, label, ui::palette::kTextPrimary, 2.0f);
        x += ui_->textSize(label, 2.0f).x + 22.0f;
    }
    char dayBuf[64];
    std::snprintf(dayBuf, sizeof dayBuf, "DAY %d  %02d:00  x%.0f%s", world_.day(),
                 static_cast<int>(world_.hourOfDay()), world_.speed(), world_.paused() ? "  PAUSED" : "");
    ui_->text({screenW - 260, 10}, dayBuf, ui::palette::kAmberBright, 2.0f);

    // Notification toasts, newest at top-right.
    renderNotifications();

    if (buildMode_ == BuildMode::Placing) {
        const std::string msg = std::string("BUILDING: ") + roomTypeName(selectedRoomType_) + "  (CLICK TO PLACE, ESC TO CANCEL)";
        ui_->text({12, 44}, msg, ui::palette::kAmber, 2.0f);
    }
    if (selectedRoom_ != kNoRoom) renderRoomInspector();

    ui_->text({12, static_cast<f32>(dm.pointHeight) - 24},
             "1-9 BUILD  LMB SELECT/PLACE  RMB ORBIT  WASD PAN  U UPGRADE  R RUSH  C COLLECT  F AUTO-ASSIGN  TAB CYCLE  SPACE PAUSE  F3 DEBUG",
             ui::palette::kTextDim, 1.4f);
}

void App::renderNotifications() {
    const platform::DisplayMetrics dm = window_->metrics();
    f32 y = 44.0f;
    const auto& notes = world_.notifications();
    const size_t start = notes.size() > 6 ? notes.size() - 6 : 0;
    for (size_t i = notes.size(); i-- > start;) {
        const Notification& n = notes[i];
        if (n.age > 12.0f) continue;
        const f32 alpha = n.age > 10.0f ? (1.0f - (n.age - 10.0f) * 0.5f) : 1.0f;
        (void)alpha;
        ui_->rect({static_cast<f32>(dm.pointWidth) - 340, y}, {328, 24}, ui::palette::kPanelBg);
        ui_->text({static_cast<f32>(dm.pointWidth) - 332, y + 5}, n.text, severityColor(n.severity), 1.6f);
        y += 28.0f;
    }
}

void App::renderRoomInspector() {
    const Room* room = world_.shelter().room(selectedRoom_);
    if (!room) { selectedRoom_ = kNoRoom; return; }
    const RoomDef& def = room->def();
    ui_->rect({12, 70}, {300, 150}, ui::palette::kPanelBg);
    ui_->rectOutline({12, 70}, {300, 150}, ui::palette::kPanelBorder);
    ui_->text({22, 80}, def.name, ui::palette::kAmberBright, 2.0f);
    char buf[128];
    std::snprintf(buf, sizeof buf, "LEVEL %d/%d  COND %d%%", room->level, def.maxLevel,
                 static_cast<int>(room->condition * 100));
    ui_->text({22, 104}, buf, ui::palette::kTextPrimary, 1.6f);
    std::snprintf(buf, sizeof buf, "WORKERS %d/%d", static_cast<int>(room->workers.size()), room->workerSlots());
    ui_->text({22, 122}, buf, ui::palette::kTextPrimary, 1.6f);
    if (def.function == RoomFunction::Produce || def.function == RoomFunction::Craft) {
        std::snprintf(buf, sizeof buf, "STORED %d %s", static_cast<int>(room->storedOutput), resourceShortName(def.produces));
        ui_->text({22, 140}, buf, ui::palette::kTextPrimary, 1.6f);
    }
    if (room->broken) ui_->text({22, 158}, "BROKEN DOWN", ui::palette::kCritical, 1.6f);
    else if (room->fire > 0.05f) ui_->text({22, 158}, "ON FIRE", ui::palette::kCritical, 1.6f);
    ui_->text({22, 190}, "U:UPGRADE  R:RUSH  C:COLLECT", ui::palette::kTextDim, 1.4f);
}

void App::renderDebugPanel() {
    debugData_.backend = device_->backend();
    debugData_.gpuName = device_->deviceInfo().gpuName;
    debugData_.apiVersion = device_->deviceInfo().apiVersion;
    debugData_.frameTimeMs = lastFrameTime_ * 1000.0;
    debugData_.fps = fpsHistory_.average();
    debugData_.cpuFrameTimeMs = lastFrameTime_ * 1000.0;
    debugData_.gpuFrameTimeMs = device_->lastGpuFrameMs();
    const gfx::FrameStats& fs = device_->lastFrameStats();
    debugData_.drawCalls = fs.drawCalls;
    debugData_.instancedDrawCalls = fs.instancedDrawCalls;
    debugData_.triangles = fs.triangles;
    const platform::DisplayMetrics dm = window_->metrics();
    debugData_.pixelWidth = dm.pixelWidth; debugData_.pixelHeight = dm.pixelHeight;
    debugData_.renderScale = settings_.graphics.renderScale;
    debugData_.residentMemoryBytes = device_->residentMemoryBytes();
    debugData_.population = world_.population();
    debugData_.activeEmergencies = static_cast<i32>(world_.events().activeCount());
    debugData_.activeExpeditions = static_cast<i32>(world_.expeditions().activeCount());
    debugData_.simGameDay = static_cast<f32>(world_.day());

    ui_->rect({static_cast<f32>(dm.pointWidth) - 300, static_cast<f32>(dm.pointHeight) - 320}, {288, 300},
             ui::palette::kPanelBg);
    f32 y = static_cast<f32>(dm.pointHeight) - 312;
    const f32 x = static_cast<f32>(dm.pointWidth) - 292;
    auto line = [&](const std::string& s, u32 color = ui::palette::kTextPrimary) {
        ui_->text({x, y}, s, color, 1.6f); y += 16.0f;
    };
    char buf[128];
    line("DEBUG PANEL (F3)", ui::palette::kAmberBright);
    line(std::string("BACKEND: ") + gfx::backendName(debugData_.backend));
    line("GPU: " + debugData_.gpuName);
    line("API: " + debugData_.apiVersion);
    std::snprintf(buf, sizeof buf, "FPS: %.0f  FRAME: %.2fms", debugData_.fps, debugData_.frameTimeMs);
    line(buf);
    std::snprintf(buf, sizeof buf, "CPU: %.2fms  GPU: %.2fms", debugData_.cpuFrameTimeMs, debugData_.gpuFrameTimeMs);
    line(buf);
    std::snprintf(buf, sizeof buf, "DRAWS: %u (+%u instanced)", debugData_.drawCalls, debugData_.instancedDrawCalls);
    line(buf);
    std::snprintf(buf, sizeof buf, "TRIS: %llu", static_cast<unsigned long long>(debugData_.triangles));
    line(buf);
    std::snprintf(buf, sizeof buf, "RES: %dx%d  SCALE %.2f", debugData_.pixelWidth, debugData_.pixelHeight, debugData_.renderScale);
    line(buf);
    std::snprintf(buf, sizeof buf, "GPU MEM: %.1f MB", static_cast<double>(debugData_.residentMemoryBytes) / (1024.0 * 1024.0));
    line(buf);
    std::snprintf(buf, sizeof buf, "POP: %d  EMERGENCIES: %d  EXPEDITIONS: %d", debugData_.population,
                 debugData_.activeEmergencies, debugData_.activeExpeditions);
    line(buf);
    std::snprintf(buf, sizeof buf, "DAY %d", static_cast<int>(debugData_.simGameDay));
    line(buf);
}

} // namespace hv::game
