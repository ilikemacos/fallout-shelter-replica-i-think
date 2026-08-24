#pragma once
// A small immediate-mode 2D renderer for the HUD: coloured quads, panels
// with rounded-looking corners (via nine-slice-free simple insets), and
// text from the built-in bitmap font. Runs its own minimal GL pipeline
// (separate from the 3D scene) so it stays simple and always available.
#include "core/Math.hpp"
#include <memory>
#include <string>
#include <vector>

namespace hv::ui {

struct UIVertex { Vec2 pos; Vec2 uv; u32 color; };

/// Backend-specific implementation lives in UIRendererGL.cpp (macOS/OpenGL).
class UIRenderer {
public:
    static std::unique_ptr<UIRenderer> create();
    virtual ~UIRenderer() = default;

    virtual void beginFrame(i32 pixelWidth, i32 pixelHeight, f32 backingScale) = 0;
    virtual void endFrame() = 0;   ///< issues the actual GL draw calls

    void rect(Vec2 pos, Vec2 size, u32 color);
    void rectOutline(Vec2 pos, Vec2 size, u32 color, f32 thickness = 1.0f);
    /// A filled rect whose width represents `fraction` of `size.x` — the
    /// resource bar / health bar / progress bar primitive.
    void meter(Vec2 pos, Vec2 size, f32 fraction, u32 fillColor, u32 backColor);
    void text(Vec2 pos, const std::string& s, u32 color, f32 scale = 2.0f);
    Vec2 textSize(const std::string& s, f32 scale = 2.0f) const;
    void line(Vec2 a, Vec2 b, u32 color, f32 thickness = 1.0f);

    Vec2 screenSize() const { return screenSize_; }
    /// Point-space mouse hit test against the last-drawn rect list, used by
    /// App for simple button/panel click handling (App owns the logic; this
    /// just exposes the geometry helper).
    static bool pointInRect(Vec2 p, Vec2 rectPos, Vec2 rectSize) {
        return p.x >= rectPos.x && p.x <= rectPos.x + rectSize.x &&
               p.y >= rectPos.y && p.y <= rectPos.y + rectSize.y;
    }

protected:
    virtual void pushQuad(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, Vec2 uv0, Vec2 uv1, u32 color, bool textured) = 0;
    Vec2 screenSize_{1280, 720};
};

// Common colour palette — warm industrial amber for highlights, cold
// emergency red for alerts, dim steel for panels. Colours are packed as
// 0xRRGGBBAA (matches Vertex::color / UIVertex::color byte order).
constexpr u32 packRGBA(u8 r, u8 g, u8 b, u8 a = 255) {
    return (static_cast<u32>(r)) | (static_cast<u32>(g) << 8) |
           (static_cast<u32>(b) << 16) | (static_cast<u32>(a) << 24);
}
namespace palette {
constexpr u32 kPanelBg      = packRGBA(0x1B, 0x1E, 0x22, 0xE6);
constexpr u32 kPanelBorder  = packRGBA(0x3A, 0x36, 0x2F, 0xFF);
constexpr u32 kTextPrimary  = packRGBA(0xE7, 0xDF, 0xC9, 0xFF);
constexpr u32 kTextDim      = packRGBA(0x8B, 0x82, 0x72, 0xFF);
constexpr u32 kAmber        = packRGBA(0xE0, 0x9D, 0x3B, 0xFF);
constexpr u32 kAmberBright  = packRGBA(0xFF, 0xC4, 0x5C, 0xFF);
constexpr u32 kGood         = packRGBA(0x5F, 0xAE, 0x5F, 0xFF);
constexpr u32 kWarning      = packRGBA(0xE8, 0xA8, 0x3F, 0xFF);
constexpr u32 kCritical     = packRGBA(0xD6, 0x46, 0x46, 0xFF);
constexpr u32 kBarBack      = packRGBA(0x1C, 0x1F, 0x24, 0xFF);
}

} // namespace hv::ui
