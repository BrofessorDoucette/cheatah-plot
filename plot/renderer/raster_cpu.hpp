#pragma once

/**
 * @file raster_cpu.hpp
 * @brief The reference rasterizer — the EXECUTABLE SPEC of the raster kernels.
 *
 * Every expression here is written to port 1:1 to the Slang source (same float ops, same
 * integer blend), because this file is three things at once: the CPU fallback on machines with
 * no GPU, the emulated-Metal stand-in (bit-exact by construction — it IS the same code), and
 * the Valgrind/memcheck target. Coverage is computed in float (0..1), quantized ONCE to 0..255,
 * and blended src-over in pure integer arithmetic — so two runs, or two backends sharing the
 * quantization, produce identical bytes.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "binning.hpp"
#include "kernels.hpp"

namespace cheatah::plot::renderer {

namespace detail {

/// Clamp to [0, 1] — the AA feather window. @complexity O(1). @alloc none.
inline float sat(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/// Coverage of pixel center (px, py) by a stroked, optionally dashed segment: 1px feather on
/// the distance to the segment, dash phase measured along it. @complexity O(1). @alloc none.
inline float cov_seg(const Prim& p, float px, float py) {
    const float ax = p.f[0], ay = p.f[1], bx = p.f[2], by = p.f[3];
    const float dx = bx - ax, dy = by - ay;
    const float len2 = dx * dx + dy * dy;
    float t = 0.0f;
    if (len2 > 0.0f) t = sat(((px - ax) * dx + (py - ay) * dy) / len2);
    const float cx = ax + t * dx, cy = ay + t * dy;
    const float d = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
    float cov = sat(p.f[4] + 0.5f - d);
    const float on = p.f[5], off = p.f[6];
    if (on > 0.0f && off > 0.0f) {
        const float along = t * std::sqrt(len2);
        const float phase = along - std::floor(along / (on + off)) * (on + off);
        if (phase >= on) cov = 0.0f;
    }
    return cov;
}

/// Coverage by a filled disc: 1px feather on the radial distance. @complexity O(1). @alloc none.
inline float cov_disc(const Prim& p, float px, float py) {
    const float d = std::sqrt((px - p.f[0]) * (px - p.f[0]) + (py - p.f[1]) * (py - p.f[1]));
    return sat(p.f[2] + 0.5f - d);
}

/// Coverage by an axis-aligned rect — filled (exact box AA), or an outline band when
/// f[4] > 0 (feathered distance to the box boundary). @complexity O(1). @alloc none.
inline float cov_rect(const Prim& p, float px, float py) {
    const float x0 = p.f[0], y0 = p.f[1], x1 = p.f[2], y1 = p.f[3];
    if (p.f[4] > 0.0f) {
        const float mx = std::max(x0 - px, px - x1);
        const float my = std::max(y0 - py, py - y1);
        const float outside = std::sqrt(std::max(mx, 0.0f) * std::max(mx, 0.0f) +
                                        std::max(my, 0.0f) * std::max(my, 0.0f));
        const float inside = std::max(mx, my);          // negative depth when inside
        const float d = outside > 0.0f ? outside : inside;
        return sat(p.f[4] + 0.5f - std::abs(d));
    }
    const float cx = sat(std::min(px + 0.5f, x1) - std::max(px - 0.5f, x0));
    const float cy = sat(std::min(py + 0.5f, y1) - std::max(py - 0.5f, y0));
    return cx * cy;
}

/// Coverage by a filled triangle: 1px feather on the min signed edge distance (winding
/// normalized by the doubled signed area). @complexity O(1). @alloc none.
inline float cov_tri(const Prim& p, float px, float py) {
    const float x0 = p.f[0], y0 = p.f[1], x1 = p.f[2], y1 = p.f[3], x2 = p.f[4], y2 = p.f[5];
    const float area2 = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (area2 == 0.0f) return 0.0f;
    const float sign = area2 > 0.0f ? 1.0f : -1.0f;
    auto edge = [&](float ex0, float ey0, float ex1, float ey1) {
        const float ex = ex1 - ex0, ey = ey1 - ey0;
        const float elen = std::sqrt(ex * ex + ey * ey);
        if (elen == 0.0f) return -1.0f;
        return sign * ((px - ex0) * ey - (py - ey0) * ex) / elen;   // signed inward distance
    };
    const float d0 = edge(x0, y0, x1, y1);
    const float d1 = edge(x1, y1, x2, y2);
    const float d2 = edge(x2, y2, x0, y0);
    return sat(0.5f - std::max(d0, std::max(d1, d2)));
}

/// Coverage by an 8×16 glyph: a pixel-aligned bitmap test, no AA (crisp text).
/// @complexity O(1). @alloc none.
inline float cov_glyph(const Prim& p, float px, float py) {
    const int gx = static_cast<int>(std::floor(px - p.f[0]));
    const int gy = static_cast<int>(std::floor(py - p.f[1]));
    if (gx < 0 || gx >= kGlyphWidth || gy < 0 || gy >= kGlyphHeight) return 0.0f;
    const std::uint32_t row = (p.u[gy / 4] >> (8 * (gy % 4))) & 0xFFu;
    return ((row >> (7 - gx)) & 1u) != 0u ? 1.0f : 0.0f;
}

/// The ONE quantization from float coverage to the integer blend. @complexity O(1). @alloc none.
inline std::uint32_t quantize_cov(float cov) {
    return static_cast<std::uint32_t>(sat(cov) * 255.0f + 0.5f);
}

/// Integer src-over: blend src (packed RGBA8, its alpha scaled by cov8) over dst. The rounding
/// form (x + 127) / 255 keeps the blend exact-integer and monotonic. @complexity O(1). @alloc none.
inline std::uint32_t blend_over(std::uint32_t dst, std::uint32_t src, std::uint32_t cov8) {
    const std::uint32_t sa = ((src >> 24) & 0xFFu) * cov8 / 255u;   // effective src alpha 0..255
    if (sa == 0u) return dst;
    std::uint32_t out = 0;
    for (int ch = 0; ch < 3; ++ch) {
        const std::uint32_t s = (src >> (8 * ch)) & 0xFFu;
        const std::uint32_t d = (dst >> (8 * ch)) & 0xFFu;
        const std::uint32_t o = (s * sa + d * (255u - sa) + 127u) / 255u;
        out |= o << (8 * ch);
    }
    const std::uint32_t da = (dst >> 24) & 0xFFu;
    const std::uint32_t oa = sa + da * (255u - sa) / 255u;
    return out | (oa << 24);
}

/// Coverage dispatch over the primitive kinds. @complexity O(1). @alloc none.
inline float coverage(const Prim& p, float px, float py) {
    switch (static_cast<PrimType>(p.type)) {
        case PrimType::kSeg: return cov_seg(p, px, py);
        case PrimType::kDisc: return cov_disc(p, px, py);
        case PrimType::kRect: return cov_rect(p, px, py);
        case PrimType::kTri: return cov_tri(p, px, py);
        case PrimType::kGlyph: return cov_glyph(p, px, py);
    }
    return 0.0f;
}

}  // namespace detail

/**
 * Rasterize a binned draw list into an RGBA8 framebuffer (one uint32 per pixel, row-major) —
 * the reference implementation of `plot_clear` + `plot_raster`.
 *
 * Every pixel starts at the clear colour, then blends its tile's primitives in paint order
 * with the integer src-over — the same loop the kernels run, one thread per pixel.
 *
 * @param prims The draw list, in paint order.
 * @param bins The tile bins for @p prims (from @ref bin_prims, same width/height).
 * @param params The framebuffer size, tile stride, and clear colour.
 * @return The framebuffer, params.width*params.height packed pixels.
 * @complexity O(pixels + Σ per-tile prim work).
 * @alloc the returned framebuffer.
 * @test plot:raster
 */
inline std::vector<std::uint32_t> raster_cpu(const std::vector<Prim>& prims, const TileBins& bins,
                                             const RasterParams& params) {
    std::vector<std::uint32_t> fb(
        static_cast<std::size_t>(params.width) * static_cast<std::size_t>(params.height),
        params.clear);
    for (std::uint32_t y = 0; y < params.height; ++y) {
        const std::uint32_t ty = y / kTile;
        for (std::uint32_t x = 0; x < params.width; ++x) {
            const std::uint32_t tx = x / kTile;
            const std::size_t tile =
                static_cast<std::size_t>(ty) * params.tiles_x + tx;
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            std::uint32_t pixel = params.clear;
            for (std::uint32_t k = bins.tile_offsets[tile]; k < bins.tile_offsets[tile + 1];
                 ++k) {
                const Prim& p = prims[bins.tile_prims[k]];
                const std::uint32_t cov8 = detail::quantize_cov(detail::coverage(p, px, py));
                if (cov8 != 0u) pixel = detail::blend_over(pixel, p.rgba, cov8);
            }
            fb[static_cast<std::size_t>(y) * params.width + x] = pixel;
        }
    }
    return fb;
}

}  // namespace cheatah::plot::renderer
