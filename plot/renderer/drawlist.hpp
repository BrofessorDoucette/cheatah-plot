#pragma once

/**
 * @file drawlist.hpp
 * @brief The CPU-side primitive list in PAINT ORDER, with typed push helpers — what the reduce
 *        step emits and the rasterizer (GPU kernel or CPU reference) consumes.
 *
 * A DrawList is just `std::vector<Prim>`: order IS z-order (later prims blend over earlier
 * ones), and the binning step preserves it per tile, so blending stays deterministic. The push
 * helpers do the lane packing for each @ref PrimType so nothing else in the renderer touches
 * raw lanes.
 */

#include <string>
#include <vector>

#include "font8x16.hpp"
#include "kernels.hpp"

namespace cheatah::plot::renderer {

/// The primitive list, in paint order. Push through the helpers below.
using DrawList = std::vector<Prim>;

/**
 * Append a stroked segment.
 *
 * @param dl The list to append to.
 * @param x0 Start x (pixels). @param y0 Start y.
 * @param x1 End x. @param y1 End y.
 * @param half_width Half the stroke width in pixels (>= 0.5 for a hairline).
 * @param rgba The packed colour.
 * @param dash_on Dash drawn-length in pixels (0 = solid).
 * @param dash_off Dash gap-length in pixels.
 * @complexity O(1) amortized.
 * @alloc amortized vector growth.
 * @test plot:drawlist
 */
inline void push_seg(DrawList& dl, float x0, float y0, float x1, float y1, float half_width,
                     std::uint32_t rgba, float dash_on = 0.0f, float dash_off = 0.0f) {
    Prim p{};
    p.type = static_cast<std::uint32_t>(PrimType::kSeg);
    p.rgba = rgba;
    p.f[0] = x0; p.f[1] = y0; p.f[2] = x1; p.f[3] = y1;
    p.f[4] = half_width; p.f[5] = dash_on; p.f[6] = dash_off;
    dl.push_back(p);
}

/**
 * Append a filled disc (scatter/stem markers).
 *
 * @param dl The list to append to.
 * @param cx Center x (pixels). @param cy Center y.
 * @param radius The disc radius in pixels.
 * @param rgba The packed colour.
 * @complexity O(1) amortized.
 * @alloc amortized vector growth.
 * @test plot:drawlist
 */
inline void push_disc(DrawList& dl, float cx, float cy, float radius, std::uint32_t rgba) {
    Prim p{};
    p.type = static_cast<std::uint32_t>(PrimType::kDisc);
    p.rgba = rgba;
    p.f[0] = cx; p.f[1] = cy; p.f[2] = radius;
    dl.push_back(p);
}

/**
 * Append an axis-aligned rectangle — filled, or an outline when @p outline_half_width > 0
 * (bars, heatmap cells, legend swatches, square markers, gridline boxes).
 *
 * @param dl The list to append to.
 * @param x0 Left. @param y0 Top. @param x1 Right. @param y1 Bottom (x0<x1, y0<y1).
 * @param rgba The packed colour.
 * @param outline_half_width 0 for a filled rect; else half the outline stroke width.
 * @complexity O(1) amortized.
 * @alloc amortized vector growth.
 * @test plot:drawlist
 */
inline void push_rect(DrawList& dl, float x0, float y0, float x1, float y1, std::uint32_t rgba,
                      float outline_half_width = 0.0f) {
    Prim p{};
    p.type = static_cast<std::uint32_t>(PrimType::kRect);
    p.rgba = rgba;
    p.f[0] = x0; p.f[1] = y0; p.f[2] = x1; p.f[3] = y1;
    p.f[4] = outline_half_width;
    dl.push_back(p);
}

/**
 * Append a filled triangle (area fills are fan-triangulated into these).
 *
 * @param dl The list to append to.
 * @param x0 Vertex 0 x. @param y0 Vertex 0 y.
 * @param x1 Vertex 1 x. @param y1 Vertex 1 y.
 * @param x2 Vertex 2 x. @param y2 Vertex 2 y.
 * @param rgba The packed colour.
 * @complexity O(1) amortized.
 * @alloc amortized vector growth.
 * @test plot:drawlist
 */
inline void push_tri(DrawList& dl, float x0, float y0, float x1, float y1, float x2, float y2,
                     std::uint32_t rgba) {
    Prim p{};
    p.type = static_cast<std::uint32_t>(PrimType::kTri);
    p.rgba = rgba;
    p.f[0] = x0; p.f[1] = y0; p.f[2] = x1; p.f[3] = y1; p.f[4] = x2; p.f[5] = y2;
    dl.push_back(p);
}

/**
 * Append one 8×16 glyph at a pixel position — the glyph's bitmap rides IN the prim's aux
 * lanes (4 bits-rows per lane), so the kernel needs no font atlas.
 *
 * @param dl The list to append to.
 * @param x Left edge (pixels). @param y Top edge.
 * @param c The character (the font substitutes '?' outside printable ASCII).
 * @param rgba The packed colour.
 * @complexity O(1) amortized.
 * @alloc amortized vector growth.
 * @test plot:drawlist
 */
inline void push_glyph(DrawList& dl, float x, float y, char c, std::uint32_t rgba) {
    Prim p{};
    p.type = static_cast<std::uint32_t>(PrimType::kGlyph);
    p.rgba = rgba;
    p.f[0] = x; p.f[1] = y;
    const auto& rows = glyph(c);
    for (int lane = 0; lane < 4; ++lane) {
        std::uint32_t packed = 0;
        for (int r = 0; r < 4; ++r)
            packed |= static_cast<std::uint32_t>(rows[static_cast<std::size_t>(lane * 4 + r)])
                      << (8 * r);
        p.u[lane] = packed;
    }
    dl.push_back(p);
}

/**
 * Append a text run, left-aligned at (@p x, @p y) top-left, one glyph prim per character.
 *
 * @param dl The list to append to.
 * @param x Left edge of the first glyph (pixels). @param y Top edge.
 * @param text The characters to draw.
 * @param rgba The packed colour.
 * @complexity O(len).
 * @alloc amortized vector growth (len prims).
 * @test plot:drawlist
 */
inline void push_text(DrawList& dl, float x, float y, const std::string& text,
                      std::uint32_t rgba) {
    for (std::size_t i = 0; i < text.size(); ++i)
        push_glyph(dl, x + static_cast<float>(i) * static_cast<float>(kGlyphWidth), y, text[i],
                   rgba);
}

}  // namespace cheatah::plot::renderer
