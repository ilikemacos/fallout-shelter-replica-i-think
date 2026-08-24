// Shared, backend-independent UI drawing logic (geometry only). The actual
// GL submission lives in UIRendererGL.cpp.
#include "ui/UIRenderer.hpp"
#include "ui/BitmapFont.hpp"

namespace hv::ui {

void UIRenderer::rect(Vec2 pos, Vec2 size, u32 color) {
    pushQuad(pos, {pos.x + size.x, pos.y}, {pos.x + size.x, pos.y + size.y}, {pos.x, pos.y + size.y},
            {0,0}, {0,0}, color, false);
}

void UIRenderer::rectOutline(Vec2 pos, Vec2 size, u32 color, f32 t) {
    rect(pos, {size.x, t}, color);
    rect({pos.x, pos.y + size.y - t}, {size.x, t}, color);
    rect(pos, {t, size.y}, color);
    rect({pos.x + size.x - t, pos.y}, {t, size.y}, color);
}

void UIRenderer::meter(Vec2 pos, Vec2 size, f32 fraction, u32 fillColor, u32 backColor) {
    rect(pos, size, backColor);
    const f32 f = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
    if (f > 0.0f) rect(pos, {size.x * f, size.y}, fillColor);
}

void UIRenderer::line(Vec2 a, Vec2 b, u32 color, f32 thickness) {
    const Vec2 d = b - a;
    const f32 len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len < 1e-4f) return;
    const Vec2 n{-d.y / len * thickness * 0.5f, d.x / len * thickness * 0.5f};
    pushQuad(a - n, a + n, b + n, b - n, {0,0}, {0,0}, color, false);
}

Vec2 UIRenderer::textSize(const std::string& s, f32 scale) const {
    const f32 advance = (kGlyphWidth + 1) * scale;
    return Vec2{advance * static_cast<f32>(s.size()), kGlyphHeight * scale};
}

void UIRenderer::text(Vec2 pos, const std::string& s, u32 color, f32 scale) {
    f32 x = pos.x;
    for (char c : s) {
        if (c == '\n') { x = pos.x; pos.y += kGlyphHeight * scale + scale * 2.0f; continue; }
        const Glyph& g = glyphFor(c);
        for (int col = 0; col < kGlyphWidth; ++col) {
            for (int row = 0; row < kGlyphHeight; ++row) {
                if (!(g.col[col] & (1u << row))) continue;
                const Vec2 p{ x + static_cast<f32>(col) * scale, pos.y + static_cast<f32>(row) * scale };
                rect(p, {scale, scale}, color);
            }
        }
        x += (kGlyphWidth + 1) * scale;
    }
}

} // namespace hv::ui
