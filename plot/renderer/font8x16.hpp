#pragma once

/**
 * @file font8x16.hpp
 * @brief The renderer's ORIGINAL 8×16 bitmap font — every tick label, axis label, title, and
 *        legend entry is drawn from these glyphs, one byte per row, bit 7 = leftmost pixel.
 *
 * This font is original work drawn for cheatah-plot: every glyph was authored by hand on a
 * 5×7 core grid (the readable per-glyph patterns in the table below, with their ASCII art);
 * no existing font's bitmaps were copied or traced. Each 5×7 pattern is expanded into the
 * 8×16 cell @ref kGlyphWidth × @ref kGlyphHeight from kernels.hpp with a 1-pixel left
 * bearing (core columns occupy cell columns 1..5), the core at cell rows 4..10 (baseline =
 * row 10), and descenders (g, j, p, q, y) shifted down 2 rows so their tails reach rows
 * 11..12. Rows 0..1 and 14..15 always stay clear — line spacing is built into the cell.
 * Printable ASCII 32..126 is covered (95 glyphs); anything else renders as '?'.
 */

#include <array>
#include <cstdint>
#include <string>

#include "kernels.hpp"

namespace cheatah::plot::renderer {

namespace detail {

/// One authored glyph: seven 5-bit core rows (bit 4 = leftmost core column) plus the
/// descender flag that shifts the whole pattern down @ref kDescenderShift cell rows.
/// @complexity n/a (plain data). @alloc none.
struct Glyph5x7 {
    std::uint8_t rows[7];  ///< the 5×7 core pattern, top row first.
    bool descender;        ///< true for g, j, p, q, y — the tail dips below the baseline.
};

/// The character code of the first authored glyph (' ') — table index 0.
inline constexpr int kFirstGlyph = 32;
/// The character code of the last authored glyph ('~').
inline constexpr int kLastGlyph = 126;
/// The cell row that holds a non-descender pattern's top row (core spans rows 4..10).
inline constexpr int kCoreTopRow = 4;
/// How many extra rows a descender glyph shifts down (tail rows land at 11..12).
inline constexpr int kDescenderShift = 2;

/// The 95 authored 5×7 patterns for ASCII 32..126, in code order. Original work — drawn for
/// cheatah-plot, not copied from any font (the art comment beside each entry IS the glyph).
/// @complexity n/a (plain data). @alloc none (static storage).
inline constexpr std::array<Glyph5x7, 95> kGlyphPatterns = {{
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x00}, false},  // ' ' ...../...../...../...../...../...../.....
    {{0x04,0x04,0x04,0x04,0x04,0x00,0x04}, false},  // '!' ..#../..#../..#../..#../..#../...../..#..
    {{0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, false},  // '"' .#.#./.#.#./...../...../...../...../.....
    {{0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}, false},  // '#' .#.#./.#.#./#####/.#.#./#####/.#.#./.#.#.
    {{0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, false},  // '$' ..#../.####/#.#../.###./..#.#/####./..#..
    {{0x18,0x19,0x02,0x04,0x08,0x13,0x03}, false},  // '%' ##.../##..#/...#./..#../.#.../#..##/...##
    {{0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, false},  // '&' .##../#..#./#.#../.#.../#.#.#/#..#./.##.#
    {{0x04,0x04,0x08,0x00,0x00,0x00,0x00}, false},  // '\'' ..#../..#../.#.../...../...../...../.....
    {{0x02,0x04,0x08,0x08,0x08,0x04,0x02}, false},  // '(' ...#./..#../.#.../.#.../.#.../..#../...#.
    {{0x08,0x04,0x02,0x02,0x02,0x04,0x08}, false},  // ')' .#.../..#../...#./...#./...#./..#../.#...
    {{0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, false},  // '*' ...../..#../#.#.#/.###./#.#.#/..#../.....
    {{0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, false},  // '+' ...../..#../..#../#####/..#../..#../.....
    {{0x00,0x00,0x00,0x00,0x0C,0x04,0x08}, false},  // ',' ...../...../...../...../.##../..#../.#...
    {{0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, false},  // '-' ...../...../...../#####/...../...../.....
    {{0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, false},  // '.' ...../...../...../...../...../.##../.##..
    {{0x01,0x01,0x02,0x04,0x08,0x10,0x10}, false},  // '/' ....#/....#/...#./..#../.#.../#..../#....
    {{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, false},  // '0' .###./#...#/#..##/#.#.#/##..#/#...#/.###.
    {{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, false},  // '1' ..#../.##../..#../..#../..#../..#../.###.
    {{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, false},  // '2' .###./#...#/....#/...#./..#../.#.../#####
    {{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, false},  // '3' #####/...#./..#../...#./....#/#...#/.###.
    {{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, false},  // '4' ...#./..##./.#.#./#..#./#####/...#./...#.
    {{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, false},  // '5' #####/#..../####./....#/....#/#...#/.###.
    {{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, false},  // '6' ..##./.#.../#..../####./#...#/#...#/.###.
    {{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, false},  // '7' #####/....#/...#./..#../.#.../.#.../.#...
    {{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, false},  // '8' .###./#...#/#...#/.###./#...#/#...#/.###.
    {{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, false},  // '9' .###./#...#/#...#/.####/....#/...#./.##..
    {{0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, false},  // ':' ...../.##../.##../...../.##../.##../.....
    {{0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08}, false},  // ';' ...../.##../.##../...../.##../..#../.#...
    {{0x02,0x04,0x08,0x10,0x08,0x04,0x02}, false},  // '<' ...#./..#../.#.../#..../.#.../..#../...#.
    {{0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, false},  // '=' ...../...../#####/...../#####/...../.....
    {{0x08,0x04,0x02,0x01,0x02,0x04,0x08}, false},  // '>' .#.../..#../...#./....#/...#./..#../.#...
    {{0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, false},  // '?' .###./#...#/....#/...#./..#../...../..#..
    {{0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E}, false},  // '@' .###./#...#/....#/.##.#/#.#.#/#.#.#/.###.
    {{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, false},  // 'A' .###./#...#/#...#/#####/#...#/#...#/#...#
    {{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, false},  // 'B' ####./#...#/#...#/####./#...#/#...#/####.
    {{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, false},  // 'C' .###./#...#/#..../#..../#..../#...#/.###.
    {{0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, false},  // 'D' ###../#..#./#...#/#...#/#...#/#..#./###..
    {{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, false},  // 'E' #####/#..../#..../####./#..../#..../#####
    {{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, false},  // 'F' #####/#..../#..../####./#..../#..../#....
    {{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, false},  // 'G' .###./#...#/#..../#.###/#...#/#...#/.####
    {{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, false},  // 'H' #...#/#...#/#...#/#####/#...#/#...#/#...#
    {{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, false},  // 'I' .###./..#../..#../..#../..#../..#../.###.
    {{0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, false},  // 'J' ..###/...#./...#./...#./...#./#..#./.##..
    {{0x11,0x12,0x14,0x18,0x14,0x12,0x11}, false},  // 'K' #...#/#..#./#.#../##.../#.#../#..#./#...#
    {{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, false},  // 'L' #..../#..../#..../#..../#..../#..../#####
    {{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, false},  // 'M' #...#/##.##/#.#.#/#.#.#/#...#/#...#/#...#
    {{0x11,0x19,0x15,0x13,0x11,0x11,0x11}, false},  // 'N' #...#/##..#/#.#.#/#..##/#...#/#...#/#...#
    {{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, false},  // 'O' .###./#...#/#...#/#...#/#...#/#...#/.###.
    {{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, false},  // 'P' ####./#...#/#...#/####./#..../#..../#....
    {{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, false},  // 'Q' .###./#...#/#...#/#...#/#.#.#/#..#./.##.#
    {{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, false},  // 'R' ####./#...#/#...#/####./#.#../#..#./#...#
    {{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, false},  // 'S' .####/#..../#..../.###./....#/....#/####.
    {{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, false},  // 'T' #####/..#../..#../..#../..#../..#../..#..
    {{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, false},  // 'U' #...#/#...#/#...#/#...#/#...#/#...#/.###.
    {{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, false},  // 'V' #...#/#...#/#...#/#...#/#...#/.#.#./..#..
    {{0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, false},  // 'W' #...#/#...#/#...#/#.#.#/#.#.#/##.##/#...#
    {{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, false},  // 'X' #...#/#...#/.#.#./..#../.#.#./#...#/#...#
    {{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, false},  // 'Y' #...#/#...#/.#.#./..#../..#../..#../..#..
    {{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, false},  // 'Z' #####/....#/...#./..#../.#.../#..../#####
    {{0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, false},  // '[' .###./.#.../.#.../.#.../.#.../.#.../.###.
    {{0x10,0x10,0x08,0x04,0x02,0x01,0x01}, false},  // '\\' #..../#..../.#.../..#../...#./....#/....#
    {{0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, false},  // ']' .###./...#./...#./...#./...#./...#./.###.
    {{0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, false},  // '^' ..#../.#.#./#...#/...../...../...../.....
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, false},  // '_' ...../...../...../...../...../...../#####
    {{0x08,0x04,0x00,0x00,0x00,0x00,0x00}, false},  // '`' .#.../..#../...../...../...../...../.....
    {{0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, false},  // 'a' ...../...../.###./....#/.####/#...#/.####
    {{0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, false},  // 'b' #..../#..../####./#...#/#...#/#...#/####.
    {{0x00,0x00,0x0E,0x10,0x10,0x11,0x0E}, false},  // 'c' ...../...../.###./#..../#..../#...#/.###.
    {{0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}, false},  // 'd' ....#/....#/.####/#...#/#...#/#...#/.####
    {{0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, false},  // 'e' ...../...../.###./#...#/#####/#..../.###.
    {{0x06,0x08,0x08,0x1C,0x08,0x08,0x08}, false},  // 'f' ..##./.#.../.#.../###../.#.../.#.../.#...
    {{0x0F,0x11,0x11,0x11,0x0F,0x01,0x0E}, true},  // 'g' .####/#...#/#...#/#...#/.####/....#/.###.
    {{0x10,0x10,0x1E,0x11,0x11,0x11,0x11}, false},  // 'h' #..../#..../####./#...#/#...#/#...#/#...#
    {{0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, false},  // 'i' ..#../...../.##../..#../..#../..#../.###.
    {{0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, true},  // 'j' ...#./...../..##./...#./...#./#..#./.##..
    {{0x10,0x10,0x12,0x14,0x18,0x14,0x12}, false},  // 'k' #..../#..../#..#./#.#../##.../#.#../#..#.
    {{0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, false},  // 'l' .##../..#../..#../..#../..#../..#../.###.
    {{0x00,0x00,0x1A,0x15,0x15,0x15,0x15}, false},  // 'm' ...../...../##.#./#.#.#/#.#.#/#.#.#/#.#.#
    {{0x00,0x00,0x1E,0x11,0x11,0x11,0x11}, false},  // 'n' ...../...../####./#...#/#...#/#...#/#...#
    {{0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, false},  // 'o' ...../...../.###./#...#/#...#/#...#/.###.
    {{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, true},  // 'p' ####./#...#/#...#/####./#..../#..../#....
    {{0x0F,0x11,0x11,0x0F,0x01,0x01,0x01}, true},  // 'q' .####/#...#/#...#/.####/....#/....#/....#
    {{0x00,0x00,0x16,0x19,0x10,0x10,0x10}, false},  // 'r' ...../...../#.##./##..#/#..../#..../#....
    {{0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}, false},  // 's' ...../...../.####/#..../.###./....#/####.
    {{0x08,0x08,0x1C,0x08,0x08,0x09,0x06}, false},  // 't' .#.../.#.../###../.#.../.#.../.#..#/..##.
    {{0x00,0x00,0x11,0x11,0x11,0x13,0x0D}, false},  // 'u' ...../...../#...#/#...#/#...#/#..##/.##.#
    {{0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, false},  // 'v' ...../...../#...#/#...#/#...#/.#.#./..#..
    {{0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, false},  // 'w' ...../...../#...#/#...#/#.#.#/#.#.#/.#.#.
    {{0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, false},  // 'x' ...../...../#...#/.#.#./..#../.#.#./#...#
    {{0x11,0x11,0x11,0x0F,0x01,0x02,0x0C}, true},  // 'y' #...#/#...#/#...#/.####/....#/...#./.##..
    {{0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, false},  // 'z' ...../...../#####/...#./..#../.#.../#####
    {{0x06,0x04,0x04,0x08,0x04,0x04,0x06}, false},  // '{' ..##./..#../..#../.#.../..#../..#../..##.
    {{0x04,0x04,0x04,0x04,0x04,0x04,0x04}, false},  // '|' ..#../..#../..#../..#../..#../..#../..#..
    {{0x0C,0x04,0x04,0x02,0x04,0x04,0x0C}, false},  // '}' .##../..#../..#../...#./..#../..#../.##..
    {{0x00,0x00,0x08,0x15,0x02,0x00,0x00}, false},  // '~' ...../...../.#.../#.#.#/...#./...../.....
}};

/// Expand one 5×7 pattern into its 8×16 cell: each 5-bit row is masked and shifted to cell
/// columns 1..5 (bit 7 = column 0 stays clear — the left bearing), placed at
/// @ref kCoreTopRow, descenders @ref kDescenderShift rows lower.
/// @return The 16 cell rows, top first. @complexity O(1) (7 fixed rows). @alloc none.
constexpr std::array<std::uint8_t, 16> expand_glyph(const Glyph5x7& g) {
    std::array<std::uint8_t, 16> cell{};
    const int top = kCoreTopRow + (g.descender ? kDescenderShift : 0);
    for (int i = 0; i < 7; ++i) {
        cell[static_cast<std::size_t>(top + i)] =
            static_cast<std::uint8_t>((g.rows[i] & 0x1Fu) << 2);
    }
    return cell;
}

/// Expand the whole pattern table into the ready-to-blit 8×16 bitmaps, at compile time.
/// @return The 95 expanded cells, in code order. @complexity O(1) (fixed 95×7 work).
/// @alloc none (constexpr static storage).
constexpr std::array<std::array<std::uint8_t, 16>, 95> build_font() {
    std::array<std::array<std::uint8_t, 16>, 95> font{};
    for (std::size_t i = 0; i < kGlyphPatterns.size(); ++i) {
        font[i] = expand_glyph(kGlyphPatterns[i]);
    }
    return font;
}

/// The expanded font: 95 glyph cells of 16 rows each, built once at compile time.
inline constexpr std::array<std::array<std::uint8_t, 16>, 95> kFont = build_font();

}  // namespace detail

/**
 * The 8×16 bitmap for character @p c — 16 rows top to bottom, one byte per row, bit 7 =
 * leftmost pixel of the cell (matching the glyph prim's row packing in kernels.hpp).
 * Characters outside printable ASCII 32..126 (including negative `char` values) return the
 * glyph for '?', so text drawing never branches on validity.
 *
 * @param c The character to look up.
 * @return The glyph's 16 row bytes (a reference into the compile-time font table).
 * @complexity O(1).
 * @alloc none.
 * @test plot:font
 */
inline const std::array<std::uint8_t, 16>& glyph(char c) {
    const int code = static_cast<unsigned char>(c);
    if (code < detail::kFirstGlyph || code > detail::kLastGlyph) {
        return detail::kFont[static_cast<std::size_t>('?' - detail::kFirstGlyph)];
    }
    return detail::kFont[static_cast<std::size_t>(code - detail::kFirstGlyph)];
}

/**
 * The pixel width of @p s drawn in this font — the font is strictly monospace, so this is
 * pure arithmetic on the length: `kGlyphWidth * len`. The layout passes (tick-label
 * centring, legend sizing) call this instead of measuring glyphs.
 *
 * @param s The text to measure.
 * @return The width in pixels of the rendered run (0 for the empty string).
 * @complexity O(1) — `size()` is constant time; no glyph is touched.
 * @alloc none.
 * @test plot:font
 */
inline int text_width(const std::string& s) {
    return kGlyphWidth * static_cast<int>(s.size());
}

}  // namespace cheatah::plot::renderer
