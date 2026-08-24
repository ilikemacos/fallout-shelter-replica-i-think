#pragma once
// A tiny built-in 5x7 pixel font — no external font file, no CoreText
// dependency, always available. Covers space, digits, uppercase A-Z and the
// punctuation the UI actually uses. Deliberately terminal/placard-styled,
// which suits the shelter's stencilled industrial signage look.
#include "core/Types.hpp"

namespace hv::ui {

/// Each glyph is 5 columns x 7 rows, one bit per pixel, column-major (bit 0 = top).
struct Glyph { u8 col[5]; };

/// Returns the glyph for an ASCII character (32..95); unknown chars render blank.
const Glyph& glyphFor(char c);
constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;

} // namespace hv::ui
