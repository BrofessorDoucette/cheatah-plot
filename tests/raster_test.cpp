// Unit tests for the renderer's font-free core: the kernel ABI (packing), tile binning (CSR
// shape, paint order, clipping), and the reference rasterizer (every coverage kind, the
// integer blend, determinism). These are the properties the GPU lanes will be held to.
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "plot/renderer/raster_cpu.hpp"

namespace rr = cheatah::plot::renderer;

namespace {

rr::Prim rect_prim(float x0, float y0, float x1, float y1, std::uint32_t rgba,
                   float outline = 0.0f) {
    rr::Prim p{};
    p.type = static_cast<std::uint32_t>(rr::PrimType::kRect);
    p.rgba = rgba;
    p.f[0] = x0; p.f[1] = y0; p.f[2] = x1; p.f[3] = y1; p.f[4] = outline;
    return p;
}

rr::Prim disc_prim(float cx, float cy, float r, std::uint32_t rgba) {
    rr::Prim p{};
    p.type = static_cast<std::uint32_t>(rr::PrimType::kDisc);
    p.rgba = rgba;
    p.f[0] = cx; p.f[1] = cy; p.f[2] = r;
    return p;
}

std::vector<std::uint32_t> run(const std::vector<rr::Prim>& prims, std::uint32_t w,
                               std::uint32_t h, std::uint32_t clear) {
    rr::TileBins bins = rr::bin_prims(prims, w, h);
    rr::RasterParams params{w, h, bins.tiles_x, clear};
    return rr::raster_cpu(prims, bins, params);
}

constexpr std::uint32_t kClear = 0xFFFFFFFFu;   // opaque white
constexpr std::uint32_t kRed = 0xFF0000FFu;     // packed r=255, a=255
constexpr std::uint32_t kBlue = 0xFFFF0000u;    // packed b=255, a=255

}  // namespace

TEST(Raster, PackRgbaQuantizesAndClamps) {
    EXPECT_EQ(rr::pack_rgba(1.0, 0.0, 0.0, 1.0), kRed);
    EXPECT_EQ(rr::pack_rgba(0.0, 0.0, 1.0, 1.0), kBlue);
    EXPECT_EQ(rr::pack_rgba(-1.0, 2.0, 0.5, 1.0) & 0xFFu, 0u);            // clamped low
    EXPECT_EQ((rr::pack_rgba(-1.0, 2.0, 0.5, 1.0) >> 8) & 0xFFu, 255u);   // clamped high
    EXPECT_EQ((rr::pack_rgba(0.0, 0.0, 0.0, 0.0) >> 24) & 0xFFu, 0u);
}

TEST(Raster, BinningShapeAndPaintOrder) {
    // Two overlapping rects in one 32x32 buffer -> 2x2 tile grid; both prims land in the
    // top-left tile with ASCENDING indices (paint order).
    std::vector<rr::Prim> prims{rect_prim(2, 2, 12, 12, kRed), rect_prim(4, 4, 14, 14, kBlue)};
    rr::TileBins bins = rr::bin_prims(prims, 32, 32);
    EXPECT_EQ(bins.tiles_x, 2u);
    EXPECT_EQ(bins.tiles_y, 2u);
    ASSERT_EQ(bins.tile_offsets.size(), 5u);
    ASSERT_GE(bins.tile_offsets[1] - bins.tile_offsets[0], 2u);
    EXPECT_LT(bins.tile_prims[bins.tile_offsets[0]], bins.tile_prims[bins.tile_offsets[0] + 1]);
    // A prim entirely off-screen bins nowhere.
    std::vector<rr::Prim> off{rect_prim(-50, -50, -40, -40, kRed)};
    rr::TileBins none = rr::bin_prims(off, 32, 32);
    EXPECT_EQ(none.tile_prims.size(), 0u);
}

TEST(Raster, ClearAndOpaqueFill) {
    auto fb = run({rect_prim(4, 4, 12, 12, kRed)}, 16, 16, kClear);
    EXPECT_EQ(fb[0], kClear);                       // outside the rect
    EXPECT_EQ(fb[8 * 16 + 8], kRed);                // deep inside: fully red
}

TEST(Raster, PaintOrderBlendsLastOverFirst) {
    // Blue painted after red wins on the overlap.
    auto fb = run({rect_prim(2, 2, 12, 12, kRed), rect_prim(6, 6, 14, 14, kBlue)}, 16, 16,
                  kClear);
    EXPECT_EQ(fb[8 * 16 + 8], kBlue);
    EXPECT_EQ(fb[3 * 16 + 3], kRed);                // red-only region stays red
}

TEST(Raster, AlphaBlendsArithmetically) {
    // 50%-alpha red over white: each channel = (src*128 + dst*127 + 127)/255 exactly.
    const std::uint32_t half_red = rr::pack_rgba(1.0, 0.0, 0.0, 128.0 / 255.0);
    auto fb = run({rect_prim(0, 0, 16, 16, half_red)}, 16, 16, kClear);
    const std::uint32_t px = fb[8 * 16 + 8];
    EXPECT_EQ(px & 0xFFu, (255u * 128u + 255u * 127u + 127u) / 255u);
    EXPECT_EQ((px >> 8) & 0xFFu, (0u * 128u + 255u * 127u + 127u) / 255u);
}

TEST(Raster, DiscCoverageCenterFullEdgeFeathered) {
    auto fb = run({disc_prim(8.0f, 8.0f, 5.0f, kRed)}, 16, 16, kClear);
    EXPECT_EQ(fb[8 * 16 + 8], kRed);                // center saturated
    EXPECT_EQ(fb[1 * 16 + 1], kClear);              // far corner untouched
    // A pixel straddling the rim blends partially: neither pure red nor pure white.
    const std::uint32_t rim = fb[8 * 16 + 12];      // center (12.5, 8.5) -> d≈4.53, cov≈0.97
    EXPECT_NE(rim, kRed);
    EXPECT_NE(rim, kClear);
}

TEST(Raster, SegmentSolidAndDashed) {
    rr::Prim seg{};
    seg.type = static_cast<std::uint32_t>(rr::PrimType::kSeg);
    seg.rgba = kRed;
    seg.f[0] = 0.0f; seg.f[1] = 8.0f; seg.f[2] = 32.0f; seg.f[3] = 8.0f;
    seg.f[4] = 1.0f;                                 // half width
    auto fb = run({seg}, 32, 16, kClear);
    EXPECT_EQ(fb[8 * 32 + 16], kRed);                // on the line
    EXPECT_EQ(fb[2 * 32 + 16], kClear);              // above it

    seg.f[5] = 4.0f; seg.f[6] = 4.0f;                // dash 4-on / 4-off
    auto fd = run({seg}, 32, 16, kClear);
    EXPECT_EQ(fd[8 * 32 + 2], kRed);                 // t=2.5 -> phase 2.5 < 4: drawn
    EXPECT_EQ(fd[8 * 32 + 6], kClear);               // t=6.5 -> phase 6.5 >= 4: gap
}

TEST(Raster, TriangleAndRectOutline) {
    rr::Prim tri{};
    tri.type = static_cast<std::uint32_t>(rr::PrimType::kTri);
    tri.rgba = kRed;
    tri.f[0] = 2.0f; tri.f[1] = 14.0f; tri.f[2] = 14.0f; tri.f[3] = 14.0f;
    tri.f[4] = 8.0f; tri.f[5] = 2.0f;
    auto fb = run({tri}, 16, 16, kClear);
    EXPECT_EQ(fb[12 * 16 + 8], kRed);                // inside the triangle
    EXPECT_EQ(fb[3 * 16 + 2], kClear);               // outside

    // Degenerate (zero-area) triangle draws nothing.
    rr::Prim degen = tri;
    degen.f[4] = 2.0f; degen.f[5] = 14.0f;           // colinear with v0
    auto fd = run({degen}, 16, 16, kClear);
    EXPECT_EQ(fd[12 * 16 + 8], kClear);

    // Rect OUTLINE: border pixels ink, interior stays clear.
    auto fo = run({rect_prim(3, 3, 13, 13, kRed, 0.75f)}, 16, 16, kClear);
    EXPECT_EQ(fo[8 * 16 + 8], kClear);               // hollow middle
    EXPECT_NE(fo[3 * 16 + 8], kClear);               // on the border
}

TEST(Raster, GlyphBitmapMapsBitsToPixels) {
    rr::Prim g{};
    g.type = static_cast<std::uint32_t>(rr::PrimType::kGlyph);
    g.rgba = kRed;
    g.f[0] = 4.0f; g.f[1] = 2.0f;
    g.u[1] = 0x80u;                                  // row 4 (lane 1, byte 0), leftmost bit
    auto fb = run({g}, 16, 24, kClear);
    EXPECT_EQ(fb[6 * 16 + 4], kRed);                 // y = 2+4, x = 4+0
    EXPECT_EQ(fb[6 * 16 + 5], kClear);               // next pixel clear
    EXPECT_EQ(fb[2 * 16 + 4], kClear);               // row 0 of the cell clear
}

TEST(Raster, DeterministicAcrossRuns) {
    std::vector<rr::Prim> prims{rect_prim(1, 1, 30, 30, rr::pack_rgba(0.3, 0.7, 0.2, 0.6)),
                                disc_prim(16, 16, 10, rr::pack_rgba(0.9, 0.1, 0.4, 0.5))};
    auto a = run(prims, 32, 32, kClear);
    auto b = run(prims, 32, 32, kClear);
    EXPECT_EQ(a, b);                                 // byte-identical, twice
}
