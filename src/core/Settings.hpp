#pragma once
// Every user-facing option lives here. The struct is plain data so it can be
// written to settings.cfg as text and diffed by the tests.
#include "core/Types.hpp"
#include <string>

namespace hv {

enum class BackendKind : u8 { Auto = 0, OpenGL = 1, Vulkan = 2 };
enum class WindowMode  : u8 { Windowed = 0, Fullscreen = 1, BorderlessFullscreen = 2 };
enum class QualityLevel: u8 { Low = 0, Medium = 1, High = 2, Ultra = 3 };
enum class RayTracingMode : u8 { Off = 0, VeryLight = 1, Low = 2 };
enum class ShadowQuality : u8 { Off = 0, Low = 1, Medium = 2, High = 3 };
enum class AOQuality     : u8 { Off = 0, Low = 1, High = 2 };
enum class ReflectionQuality : u8 { Off = 0, ScreenSpaceLow = 1, ScreenSpaceHigh = 2 };

const char* toString(BackendKind v);
const char* toString(WindowMode v);
const char* toString(RayTracingMode v);
const char* toString(ShadowQuality v);
const char* toString(AOQuality v);
const char* toString(ReflectionQuality v);
const char* toString(QualityLevel v);

struct GraphicsSettings {
    BackendKind backend = BackendKind::Auto;
    WindowMode  windowMode = WindowMode::Windowed;
    i32  windowWidth  = 1920;   ///< logical points, not pixels
    i32  windowHeight = 1080;
    f32  renderScale  = 1.0f;   ///< 0.5 .. 2.0 multiplier on the drawable
    bool vsync = true;
    i32  fpsLimit = 0;          ///< 0 = unlimited (still bounded by vsync)
    bool highDPI = true;        ///< use the Retina backing scale

    ShadowQuality shadows = ShadowQuality::High;
    AOQuality     ambientOcclusion = AOQuality::High;
    ReflectionQuality reflections = ReflectionQuality::ScreenSpaceHigh;
    RayTracingMode rayTracing = RayTracingMode::VeryLight;
    bool volumetrics = true;
    bool particles = true;
    bool bloom = true;
    bool filmGrain = true;
    i32  msaa = 0;              ///< 0/2/4 — off by default, we use a deferred-ish pass
    f32  lodBias = 1.0f;
    bool occlusionCulling = true;
    bool instancing = true;
    bool textureStreaming = true;
    bool dynamicResolution = true;  ///< drop render scale to hold the target frame rate
    i32  targetFps = 60;

    /// Bulk preset; individual toggles can still be changed afterwards.
    void applyPreset(QualityLevel q);
};

struct AudioSettings {
    f32  master = 0.8f;
    f32  music = 0.5f;
    f32  sfx = 0.9f;
    f32  ambience = 0.7f;
    bool muted = false;
};

struct GameplaySettings {
    f32  cameraSpeed = 1.0f;
    f32  zoomSpeed = 1.0f;
    bool invertDrag = false;
    bool edgeScroll = false;
    bool autosaveEnabled = true;
    i32  autosaveMinutes = 5;
    bool tutorialHints = true;
    bool pauseOnFocusLoss = true;
};

struct Settings {
    GraphicsSettings graphics;
    AudioSettings    audio;
    GameplaySettings gameplay;
    bool             showDebugPanel = false;

    /// Reads settings.cfg; missing keys keep their defaults.
    bool load(const std::string& path);
    /// Atomic write (temp file + rename) so a crash cannot truncate it.
    bool save(const std::string& path) const;
    std::string serialize() const;
    void deserialize(const std::string& text);
};

} // namespace hv
