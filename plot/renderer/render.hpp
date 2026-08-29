#pragma once

/**
 * @file render.hpp
 * @brief The renderer's front door: `render` a Figure to pixels, `save` it to a PNG file.
 *
 * The CPU reference path (reduce → bin → raster_cpu → encode) works end to end on every
 * machine — PNGs with zero GPU requirements. The GPU lanes slot in UNDER this surface: when a
 * build carries one (see gpu_lane.hpp) the SAME reduce + bins feed the `plot_clear` +
 * `plot_raster` kernels, and `render` prefers them at runtime, falling back to @ref raster_cpu
 * whenever the device cannot deliver. `CHEATAH_PLOT_FORCE_CPU=1` in the environment pins the
 * CPU path (the byte-exact reference), GPU build or not.
 */

#include <cstdint>
#include <string>

#include "png.hpp"
#include "raster_cpu.hpp"
#include "reduce.hpp"

#if defined(CHEATAH_PLOT_GPU)
#include "render_gpu.hpp"
#endif

namespace cheatah::plot::renderer {

/**
 * Render a figure to RGBA8 pixels at the figure's own size — the readback form (the stream
 * frame, the test surface, the encoder input).
 *
 * A GPU-enabled build TRIES the default GPU lane first when cheatah-gpu-linalg's `available()` reports one and
 * the environment does not say `CHEATAH_PLOT_FORCE_CPU=1`; any device failure falls back to
 * @ref raster_cpu with a one-time stderr notice, and further renders stay on the CPU. The two
 * rasterizers share the reduce, the bins and the quantization, so the choice never changes
 * what the figure MEANS — only which silicon fills the pixels.
 *
 * @param fig The figure model to draw.
 * @return The rendered image (width/height from the figure, row-major RGBA).
 * @complexity O(pixels + data); single-threaded by design.
 * @alloc the returned image + the transient draw list, bins, and framebuffer.
 * @gpualloc on the GPU path, the five transient buffers of @ref raster_gpu.
 * @test plot:render
 */
inline Image render(cheatah::figure::Figure& fig) {
    const std::uint32_t w = static_cast<std::uint32_t>(fig.width);
    const std::uint32_t h = static_cast<std::uint32_t>(fig.height);
    DrawList dl = reduce(fig, w, h);
    TileBins bins = bin_prims(dl, w, h);
    RasterParams params{w, h, bins.tiles_x, detail::kWhite};
    std::vector<std::uint32_t> fb;
#if defined(CHEATAH_PLOT_GPU)
    if (!try_gpu(dl, bins, params, fb))
#endif
        fb = raster_cpu(dl, bins, params);
    return detail::pack_image(fb, w, h);
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
