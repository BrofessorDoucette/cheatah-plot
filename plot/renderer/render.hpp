#pragma once

/**
 * @file render.hpp
 * @brief The renderer's front door: `render` a Figure to pixels, `save` it to a PNG file.
 *
 * v1 is the CPU reference path end to end (reduce → bin → raster_cpu → encode) — every machine
 * gets PNGs with zero GPU requirements. The GPU lanes slot in UNDER this surface (the same
 * reduce + bins feed the kernels), so nothing above it changes when they land.
 */

#include <cstdint>
#include <string>

#include "png.hpp"
#include "raster_cpu.hpp"
#include "reduce.hpp"

namespace cheatah::plot::renderer {

/**
 * Render a figure to RGBA8 pixels at the figure's own size — the readback form (the stream
 * frame, the test surface, the encoder input).
 *
 * @param fig The figure model to draw.
 * @return The rendered image (width/height from the figure, row-major RGBA).
 * @complexity O(pixels + data); single-threaded by design.
 * @alloc the returned image + the transient draw list, bins, and framebuffer.
 * @test plot:render
 */
inline Image render(cheatah::figure::Figure& fig) {
    const std::uint32_t w = static_cast<std::uint32_t>(fig.width);
    const std::uint32_t h = static_cast<std::uint32_t>(fig.height);
    DrawList dl = reduce(fig, w, h);
    TileBins bins = bin_prims(dl, w, h);
    RasterParams params{w, h, bins.tiles_x, detail::kWhite};
    std::vector<std::uint32_t> fb = raster_cpu(dl, bins, params);
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<std::size_t>(w) * h * 4u);
    for (std::size_t i = 0; i < fb.size(); ++i) {
        img.rgba[i * 4 + 0] = static_cast<std::uint8_t>(fb[i] & 0xFFu);
        img.rgba[i * 4 + 1] = static_cast<std::uint8_t>((fb[i] >> 8) & 0xFFu);
        img.rgba[i * 4 + 2] = static_cast<std::uint8_t>((fb[i] >> 16) & 0xFFu);
        img.rgba[i * 4 + 3] = static_cast<std::uint8_t>((fb[i] >> 24) & 0xFFu);
    }
    return img;
}

/**
 * Render a figure and write it to @p path as a PNG — the one-call "give me my plot" form.
 *
 * @param fig The figure model to draw.
 * @param path The output file path (.png).
 * @complexity O(pixels + data).
 * @alloc transient render buffers + the encoded byte stream.
 * @test plot:render
 */
inline void save(cheatah::figure::Figure& fig, const std::string& path) {
    Image img = render(fig);
    save_png(img, path);
}

}  // namespace cheatah::plot::renderer
