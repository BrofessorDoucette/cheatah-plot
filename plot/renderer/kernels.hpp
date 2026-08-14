#pragma once

/**
 * @file kernels.hpp
 * @brief The renderer's kernel ABI: the 64-byte `Prim` POD, the packed-color form, and the
 *        params layout shared VERBATIM by the Slang kernels, the C++ reference rasterizer, and
 *        the emulated-Metal stand-ins.
 *
 * The whole renderer draws five primitive kinds — segments, discs, axis-aligned rects,
 * triangles, and glyphs — src-over blended per pixel in INTEGER arithmetic (no atomics), so
 * output is deterministic and the CPU reference is bit-exact against the emulated lane. A
 * glyph prim CARRIES its 8×16 bitmap in its aux lanes, so no font atlas ever crosses the bus.
 * Primitives arrive PRE-CLIPPED (the reduce step clips to the subplot viewport), which keeps
 * the kernel branch-free of clip state.
 */

#include <cstdint>

namespace cheatah::plot::renderer {

/// The primitive kinds the raster kernel draws. Values are the on-device codes — append-only.
enum class PrimType : std::uint32_t {
    kSeg = 0,     ///< stroked segment: f[0..3]=x0,y0,x1,y1; f[4]=half width; f[5]=dash on px; f[6]=dash off px (0 = solid).
    kDisc = 1,    ///< filled disc: f[0..1]=center; f[2]=radius.
    kRect = 2,    ///< axis-aligned rect: f[0..3]=x0,y0,x1,y1 (x0<x1, y0<y1); f[4]=outline half width (0 = filled).
    kTri = 3,     ///< filled triangle: f[0..5]=x0,y0,x1,y1,x2,y2 (any winding).
    kGlyph = 4,   ///< 8×16 glyph: f[0..1]=top-left; u[0..3]=the 16 rows, 8 bits each, packed little-endian.
};

/**
 * One drawable primitive — a 64-byte POD laid out identically in C++ and Slang (16 four-byte
 * lanes; no pointers, no padding surprises). Geometry lanes `f` and aux lanes `u` are
 * per-type, documented on @ref PrimType.
 */
struct Prim {
    std::uint32_t type;   ///< the @ref PrimType code.
    std::uint32_t rgba;   ///< the packed draw colour (see @ref pack_rgba).
    float f[10];          ///< geometry lanes (per-type meaning).
    std::uint32_t u[4];   ///< aux lanes (glyph bitmap rows; otherwise 0).
};
static_assert(sizeof(Prim) == 64, "the kernel ABI is 16 four-byte lanes");

/// The raster tile edge in pixels: binning buckets primitives into kTile×kTile screen tiles.
inline constexpr std::uint32_t kTile = 16;

/// The glyph cell the kernel rasterizes: 8 pixels wide (a glyph prim's aux lanes carry the
/// bitmap, one byte per row, bit 7 = leftmost pixel).
inline constexpr int kGlyphWidth = 8;
/// The glyph cell height in pixels (16 rows = the prim's four aux lanes, 4 rows per lane).
inline constexpr int kGlyphHeight = 16;

/**
 * Pack an RGBA colour (channels 0..1) into the kernel's byte order (r | g<<8 | b<<16 | a<<24),
 * clamping each channel — the ONE quantization point between the float model layers and the
 * integer raster.
 *
 * @param r Red in [0, 1].
 * @param g Green in [0, 1].
 * @param b Blue in [0, 1].
 * @param a Alpha in [0, 1].
 * @return The packed RGBA8 value.
 * @complexity O(1).
 * @alloc none.
 * @test plot:raster
 */
inline std::uint32_t pack_rgba(double r, double g, double b, double a) {
    auto q = [](double v) -> std::uint32_t {
        if (v <= 0.0) return 0;
        if (v >= 1.0) return 255;
        return static_cast<std::uint32_t>(v * 255.0 + 0.5);
    };
    return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
}

/**
 * The uniform parameter block the kernels read (bound as a trailing uint buffer, the proven
 * emulated-Metal-compatible convention): framebuffer size, the tile grid, and the clear colour.
 */
struct RasterParams {
    std::uint32_t width;     ///< framebuffer width in pixels.
    std::uint32_t height;    ///< framebuffer height in pixels.
    std::uint32_t tiles_x;   ///< ceil(width / kTile) — the tile grid's stride.
    std::uint32_t clear;     ///< the packed background colour (plot_clear writes it everywhere).
};
static_assert(sizeof(RasterParams) == 16, "params ride one 4-lane uint vector");

}  // namespace cheatah::plot::renderer
