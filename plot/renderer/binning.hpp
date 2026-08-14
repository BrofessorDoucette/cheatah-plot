#pragma once

/**
 * @file binning.hpp
 * @brief CPU tile binning: bucket every primitive into the 16×16 screen tiles its conservative
 *        bounding box touches, preserving paint order inside each tile.
 *
 * The raster kernel is one thread per pixel; each pixel walks ONLY its tile's primitive list.
 * The flattened CSR-style layout (offsets + indices) is exactly what the kernels bind:
 * `tile_offsets[t] .. tile_offsets[t+1]` indexes `tile_prims`, and because primitives are
 * scanned in list order, per-tile indices ascend — blending order stays the paint order.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "kernels.hpp"

namespace cheatah::plot::renderer {

/// The flattened per-tile primitive index lists (CSR layout) the kernels bind.
struct TileBins {
    std::uint32_t tiles_x = 0;                 ///< tile-grid width (ceil(width / kTile)).
    std::uint32_t tiles_y = 0;                 ///< tile-grid height.
    std::vector<std::uint32_t> tile_offsets;   ///< tiles_x*tiles_y + 1 running offsets.
    std::vector<std::uint32_t> tile_prims;     ///< prim indices, ascending within each tile.
};

namespace detail {
/// A primitive's conservative pixel-space bounding box (inflated for stroke width + the 1px AA
/// feather), clamped to the framebuffer. Returns false when it misses the framebuffer entirely.
/// @complexity O(1). @alloc none.
inline bool prim_bounds(const Prim& p, std::uint32_t width, std::uint32_t height, float& out_x0,
                        float& out_y0, float& out_x1, float& out_y1) {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    switch (static_cast<PrimType>(p.type)) {
        case PrimType::kSeg: {
            const float pad = p.f[4] + 1.0f;
            x0 = std::min(p.f[0], p.f[2]) - pad;
            y0 = std::min(p.f[1], p.f[3]) - pad;
            x1 = std::max(p.f[0], p.f[2]) + pad;
            y1 = std::max(p.f[1], p.f[3]) + pad;
            break;
        }
        case PrimType::kDisc: {
            const float pad = p.f[2] + 1.0f;
            x0 = p.f[0] - pad; y0 = p.f[1] - pad;
            x1 = p.f[0] + pad; y1 = p.f[1] + pad;
            break;
        }
        case PrimType::kRect: {
            const float pad = p.f[4] + 1.0f;
            x0 = p.f[0] - pad; y0 = p.f[1] - pad;
            x1 = p.f[2] + pad; y1 = p.f[3] + pad;
            break;
        }
        case PrimType::kTri: {
            x0 = std::min(p.f[0], std::min(p.f[2], p.f[4])) - 1.0f;
            y0 = std::min(p.f[1], std::min(p.f[3], p.f[5])) - 1.0f;
            x1 = std::max(p.f[0], std::max(p.f[2], p.f[4])) + 1.0f;
            y1 = std::max(p.f[1], std::max(p.f[3], p.f[5])) + 1.0f;
            break;
        }
        case PrimType::kGlyph: {
            x0 = p.f[0]; y0 = p.f[1];
            x1 = p.f[0] + static_cast<float>(kGlyphWidth);
            y1 = p.f[1] + static_cast<float>(kGlyphHeight);
            break;
        }
    }
    if (x1 < 0.0f || y1 < 0.0f) return false;
    if (x0 >= static_cast<float>(width) || y0 >= static_cast<float>(height)) return false;
    out_x0 = std::max(x0, 0.0f);
    out_y0 = std::max(y0, 0.0f);
    out_x1 = std::min(x1, static_cast<float>(width));
    out_y1 = std::min(y1, static_cast<float>(height));
    return true;
}
}  // namespace detail

/**
 * Bucket @p prims into the tile grid covering a width×height framebuffer.
 *
 * Two passes over the primitive list (count, then fill) build the CSR layout with exactly-sized
 * vectors; inside each tile the indices ascend, preserving paint order.
 *
 * @param prims The primitive list, in paint order.
 * @param width The framebuffer width in pixels (>= 1).
 * @param height The framebuffer height in pixels (>= 1).
 * @return The per-tile index lists.
 * @complexity O(prims · tiles-they-touch).
 * @alloc the two returned vectors.
 * @test plot:binning
 */
inline TileBins bin_prims(const std::vector<Prim>& prims, std::uint32_t width, std::uint32_t height) {
    TileBins bins;
    bins.tiles_x = (width + kTile - 1) / kTile;
    bins.tiles_y = (height + kTile - 1) / kTile;
    const std::size_t ntiles =
        static_cast<std::size_t>(bins.tiles_x) * static_cast<std::size_t>(bins.tiles_y);
    bins.tile_offsets.assign(ntiles + 1, 0);

    auto tiles_of = [&](const Prim& p, auto&& per_tile) {
        float x0, y0, x1, y1;
        if (!detail::prim_bounds(p, width, height, x0, y0, x1, y1)) return;
        const std::uint32_t tx0 = static_cast<std::uint32_t>(x0) / kTile;
        const std::uint32_t ty0 = static_cast<std::uint32_t>(y0) / kTile;
        // The bounds are half-open at the max edge; subtract an epsilon-free integer step by
        // clamping the LAST covered pixel, not the exclusive edge.
        const std::uint32_t tx1 =
            std::min(bins.tiles_x - 1, static_cast<std::uint32_t>(std::max(x1 - 1.0f, 0.0f)) / kTile);
        const std::uint32_t ty1 =
            std::min(bins.tiles_y - 1, static_cast<std::uint32_t>(std::max(y1 - 1.0f, 0.0f)) / kTile);
        for (std::uint32_t ty = ty0; ty <= ty1; ++ty)
            for (std::uint32_t tx = tx0; tx <= tx1; ++tx)
                per_tile(static_cast<std::size_t>(ty) * bins.tiles_x + tx);
    };

    for (const Prim& p : prims)
        tiles_of(p, [&](std::size_t t) { ++bins.tile_offsets[t + 1]; });
    for (std::size_t t = 1; t <= ntiles; ++t) bins.tile_offsets[t] += bins.tile_offsets[t - 1];

    bins.tile_prims.assign(bins.tile_offsets[ntiles], 0);
    std::vector<std::uint32_t> cursor(bins.tile_offsets.begin(), bins.tile_offsets.end() - 1);
    for (std::size_t i = 0; i < prims.size(); ++i)
        tiles_of(prims[i], [&](std::size_t t) {
            bins.tile_prims[cursor[t]++] = static_cast<std::uint32_t>(i);
        });
    return bins;
}

}  // namespace cheatah::plot::renderer
