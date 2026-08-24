#pragma once
// Abstract platform window. CocoaWindow is the only implementation (Haven
// targets Apple Silicon macOS exclusively) but keeping this boundary means
// nothing above it references AppKit types directly.
#include "core/Settings.hpp"
#include "input/InputState.hpp"
#include <functional>
#include <memory>
#include <string>

namespace hv::platform {

struct DisplayMetrics {
    i32 pointWidth = 1280, pointHeight = 720;   ///< logical size the OS reports
    i32 pixelWidth = 1280, pixelHeight = 720;   ///< actual drawable size (Retina = 2x points)
    f32 backingScale = 1.0f;
};

class Window {
public:
    virtual ~Window() = default;

    static std::unique_ptr<Window> create(const std::string& title, const hv::GraphicsSettings& gfx);

    virtual void pumpEvents() = 0;
    virtual bool shouldClose() const = 0;
    virtual void requestClose() = 0;

    virtual const input::InputState& input() const = 0;
    virtual DisplayMetrics metrics() const = 0;

    virtual void setWindowMode(hv::WindowMode mode) = 0;
    virtual void setVSync(bool enabled) = 0;
    virtual void setTitle(const std::string& title) = 0;

    /// Makes the GL context current and swaps buffers; a no-op stand-in on
    /// backends that present differently (kept so App does not need #ifdefs).
    virtual void swapBuffers() = 0;

    /// Native handles the renderer/audio backends need. Never touched
    /// outside platform/ and renderer/gl or renderer/vk.
    virtual void* nativeWindowHandle() const = 0;
    virtual void* nativeViewHandle() const = 0;

    /// True while the window has lost focus (used for auto-pause).
    virtual bool isFocused() const = 0;
};

} // namespace hv::platform
