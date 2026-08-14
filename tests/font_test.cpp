// Unit tests for plot.renderer's original 8x16 bitmap font (plot/renderer/font8x16.hpp) — the
// plot:font suite the header's @test tags point at. The pairwise-uniqueness loop is the "font
// is actually drawn" proof: 94 stubbed or copy-pasted glyphs cannot survive it. The bounds
// test pins the cell contract the rasterizer relies on (clear padding rows, 1px left bearing).
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

#include "plot/renderer/font8x16.hpp"

namespace r = cheatah::plot::renderer;

namespace {

// Whether any pixel is set anywhere in the 16-row cell.
bool has_ink(const std::array<std::uint8_t, 16>& g) {
    for (std::uint8_t row : g) {
        if (row != 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(Font, MetricsAndTextWidth) {
    EXPECT_EQ(r::kGlyphWidth, 8);
    EXPECT_EQ(r::kGlyphHeight, 16);
    EXPECT_EQ(r::text_width("abc"), 24);
    EXPECT_EQ(r::text_width(""), 0);
    EXPECT_EQ(r::text_width("a plot title"), 8 * 12);
}

TEST(Font, LookalikeGlyphsDiffer) {
    EXPECT_NE(r::glyph('A'), r::glyph('B'));
    EXPECT_NE(r::glyph('0'), r::glyph('O')) << "zero must be distinguishable from capital O";
    EXPECT_NE(r::glyph('1'), r::glyph('l'));
    EXPECT_NE(r::glyph('I'), r::glyph('|'));
}

TEST(Font, SpaceIsEmptyEveryOtherGlyphHasInk) {
    const std::array<std::uint8_t, 16>& space = r::glyph(' ');
    for (std::size_t row = 0; row < 16; ++row) {
        EXPECT_EQ(space[row], 0u) << "space row " << row;
    }
    for (int c = 33; c <= 126; ++c) {
        EXPECT_TRUE(has_ink(r::glyph(static_cast<char>(c))))
            << "glyph " << c << " ('" << static_cast<char>(c) << "') is blank";
    }
}

TEST(Font, OutOfRangeMapsToQuestionMark) {
    const std::array<std::uint8_t, 16>& fallback = r::glyph('?');
    EXPECT_EQ(r::glyph('\t'), fallback);
    EXPECT_EQ(r::glyph('\n'), fallback);
    EXPECT_EQ(r::glyph(static_cast<char>(31)), fallback);
    EXPECT_EQ(r::glyph(static_cast<char>(127)), fallback);
    EXPECT_EQ(r::glyph(static_cast<char>(200)), fallback) << "high byte (negative char)";
}

TEST(Font, NoTwoGlyphsAreByteIdentical) {
    // The proof the font was drawn: every pair of printable glyphs (space excluded) must
    // differ somewhere in its 16 bytes.
    for (int a = 33; a <= 126; ++a) {
        for (int b = a + 1; b <= 126; ++b) {
            EXPECT_NE(r::glyph(static_cast<char>(a)), r::glyph(static_cast<char>(b)))
                << "glyphs " << a << " ('" << static_cast<char>(a) << "') and " << b << " ('"
                << static_cast<char>(b) << "') are identical";
        }
    }
}

TEST(Font, VerticalBoundsAndLeftBearingHold) {
    // Cell contract: rows 0..1 and 14..15 stay clear on EVERY glyph (line spacing lives in
    // the cell; descender tails may use rows 12..13 only), and column 0 (bit 7) stays clear —
    // the 1px left bearing that keeps adjacent glyphs from touching.
    for (int c = 32; c <= 126; ++c) {
        const std::array<std::uint8_t, 16>& g = r::glyph(static_cast<char>(c));
        EXPECT_EQ(g[0], 0u) << "glyph " << c << " row 0";
        EXPECT_EQ(g[1], 0u) << "glyph " << c << " row 1";
        EXPECT_EQ(g[14], 0u) << "glyph " << c << " row 14";
        EXPECT_EQ(g[15], 0u) << "glyph " << c << " row 15";
        for (std::size_t row = 0; row < 16; ++row) {
            EXPECT_EQ(g[row] & 0x80u, 0u)
                << "glyph " << c << " row " << row << " touches the bearing column";
        }
    }
}

TEST(Font, ExpandGlyphRuntimePathMatchesTheTable) {
    // The table is built at compile time, so llvm-cov never sees expand_glyph run; invoking it
    // at runtime both covers it and proves the expansion law: 5-bit rows land left-aligned
    // with the 1px bearing, at the core rows (descenders shifted down).
    namespace rd = cheatah::plot::renderer::detail;
    rd::Glyph5x7 bar{{0x1Fu, 0x00u, 0x1Fu, 0x00u, 0x1Fu, 0x00u, 0x1Fu}, false};
    auto cell = rd::expand_glyph(bar);
    EXPECT_EQ(cell[0], 0u);                                    // padding row stays empty
    EXPECT_EQ(cell[static_cast<std::size_t>(rd::kCoreTopRow)], (0x1Fu << 2));
    rd::Glyph5x7 tail{{0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u}, true};
    auto shifted = rd::expand_glyph(tail);
    EXPECT_EQ(shifted[static_cast<std::size_t>(rd::kCoreTopRow)], 0u);   // shifted away
    EXPECT_EQ(shifted[static_cast<std::size_t>(rd::kCoreTopRow + rd::kDescenderShift)],
              (0x11u << 2));
}

TEST(Font, BuildFontRuntimePathMatchesTheServedTable) {
    // Like expand_glyph, build_font runs at compile time for the served table; running it once
    // at runtime covers it and pins the two paths together.
    namespace rd = cheatah::plot::renderer::detail;
    // Called through a volatile fn pointer so the constexpr call cannot constant-fold away.
    std::array<std::array<std::uint8_t, 16>, 95> (*volatile fp)() = &rd::build_font;
    auto built = fp();
    EXPECT_EQ(built[static_cast<std::size_t>('A' - 32)],
              cheatah::plot::renderer::glyph('A'));
    EXPECT_EQ(built[0], cheatah::plot::renderer::glyph(' '));
}
