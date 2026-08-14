#pragma once

/**
 * @file render_gpu.hpp
 * @brief The GPU render path: `render_gpu` a Figure to pixels through the plot kernels — the
 *        SAME reduce + bins as the CPU path feed `plot_clear` + `plot_raster`, and the
 *        framebuffer comes back byte-compatible with @ref raster_cpu.
 *
 * The split of labor is deliberate: reduction and tile binning stay on the CPU (they are cheap,
 * pointer-heavy and already proven), and the per-pixel coverage + integer blend — the O(pixels)
 * part — runs on the device. On the emulated-Metal lane the "device" is raster_cpu's own code
 * registered as stand-ins, so those bytes are IDENTICAL by construction; on Vulkan the kernels
 * are the same expressions compiled by slangc, identical outside sqrt/floor ULP noise inside
 * the 1-pixel AA feather (the tests bound it per channel).
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "binning.hpp"
#include "gpu_context.hpp"
#include "kernels.hpp"
#include "png.hpp"
#include "reduce.hpp"

namespace cheatah::plot::renderer {

/**
 * Rasterize a binned draw list through the plot kernels on context @p ctx — the device mirror
 * of @ref raster_cpu: upload prims/offsets/indices/params, dispatch `plot_clear` then
 * `plot_raster` (one thread per pixel), download the framebuffer.
 *
 * @tparam Ctx The device context lane (see @ref RasterContext).
 * @param ctx The live context to dispatch on (see @ref detail::ctx_of).
 * @param prims The draw list, in paint order.
 * @param bins The tile bins for @p prims (from @ref bin_prims, same width/height).
 * @param params The framebuffer size, tile stride, and clear colour.
 * @return The framebuffer, params.width*params.height packed pixels.
 * @complexity O(pixels + Σ per-tile prim work) device-side; O(prims + tiles) transfer.
 * @alloc the returned framebuffer.
 * @gpualloc five transient device buffers (prims, offsets, indices, framebuffer, params),
 *           released before returning — also on the throw path.
 * @test plot:gpu_raster
 */
template <RasterContext Ctx>
inline std::vector<std::uint32_t> raster_gpu(Ctx& ctx, const std::vector<Prim>& prims,
                                             const TileBins& bins, const RasterParams& params) {
    const std::size_t npix = static_cast<std::size_t>(params.width) * params.height;
    if (npix == 0) return {};
    using Buf = typename Ctx::buffer_t;
    Buf* bufs[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};   // the raster binding order
    try {
        bufs[0] = ctx.new_buffer(prims.size() * sizeof(Prim));
        if (!prims.empty()) ctx.upload(bufs[0], prims.data(), prims.size() * sizeof(Prim));
        bufs[1] = ctx.new_buffer(bins.tile_offsets.size() * sizeof(std::uint32_t));
        ctx.upload(bufs[1], bins.tile_offsets.data(),
                   bins.tile_offsets.size() * sizeof(std::uint32_t));
        bufs[2] = ctx.new_buffer(bins.tile_prims.size() * sizeof(std::uint32_t));
        if (!bins.tile_prims.empty())
            ctx.upload(bufs[2], bins.tile_prims.data(),
                       bins.tile_prims.size() * sizeof(std::uint32_t));
        bufs[3] = ctx.new_buffer(npix * sizeof(std::uint32_t));
        bufs[4] = ctx.new_buffer(sizeof(RasterParams));
        ctx.upload(bufs[4], &params, sizeof(RasterParams));

        Buf* clear_bufs[2] = {bufs[3], bufs[4]};
        ctx.dispatch_2d(kClearKernel, clear_bufs, 2, params.width, params.height);
        ctx.dispatch_2d(kRasterKernel, bufs, 5, params.width, params.height);

        std::vector<std::uint32_t> fb(npix);
        ctx.download(bufs[3], fb.data(), npix * sizeof(std::uint32_t));
        for (Buf* b : bufs) ctx.release_buffer(b);
        return fb;
    } catch (...) {
        for (Buf* b : bufs)
            if (b != nullptr) ctx.release_buffer(b);
        throw;
    }
}

/**
 * Render a figure to RGBA8 pixels on the default GPU lane — the device mirror of @ref render:
 * same reduce, same bins, same packed-pixel unpack; only the rasterizer differs.
 *
 * Unlike @ref render this does NOT fall back: a dead lane or missing kernel binary throws, so
 * a caller that asked for the GPU explicitly hears exactly why it could not have it.
 *
 * @param fig The figure model to draw.
 * @return The rendered image (width/height from the figure, row-major RGBA).
 * @complexity O(pixels + data); single dispatch pair, blocking.
 * @alloc the returned image + the transient draw list, bins, and framebuffer.
 * @gpualloc the five transient buffers of @ref raster_gpu (plus one-time lane bring-up).
 * @test plot:gpu_raster
 */
inline Image render_gpu(cheatah::figure::Figure& fig) {
    const std::uint32_t w = static_cast<std::uint32_t>(fig.width);
    const std::uint32_t h = static_cast<std::uint32_t>(fig.height);
    DrawList dl = reduce(fig, w, h);
    TileBins bins = bin_prims(dl, w, h);
    RasterParams params{w, h, bins.tiles_x, detail::kWhite};
    std::vector<std::uint32_t> fb =
        raster_gpu(detail::ctx_of<detail::Context>(), dl, bins, params);
    return detail::pack_image(fb, w, h);
}

}  // namespace cheatah::plot::renderer

namespace cheatah::plot::renderer {

/**
 * Attempt the default GPU lane for one raster: honours `CHEATAH_PLOT_FORCE_CPU=1`, the cached
 * @ref gpu_available probe, and a process-wide failure latch — the FIRST device failure prints
 * a one-time stderr notice and every later render goes straight to the CPU reference (retrying
 * a broken lane per frame would re-fail and re-allocate forever).
 *
 * @param prims The draw list, in paint order.
 * @param bins The tile bins for @p prims.
 * @param params The framebuffer size, tile stride, and clear colour.
 * @param out_fb Receives the device framebuffer on success; untouched on refusal/failure.
 * @return true when the GPU produced @p out_fb; false to take the CPU path.
 * @complexity O(pixels + per-tile prim work) on the device.
 * @alloc the returned framebuffer vector on success.
 * @gpualloc the five transient buffers of @ref raster_gpu, released either way.
 * @test plot:gpu
 */
inline bool try_gpu(const std::vector<Prim>& prims, const TileBins& bins,
                    const RasterParams& params, std::vector<std::uint32_t>& out_fb) {
    const char* force = std::getenv("CHEATAH_PLOT_FORCE_CPU");
    if (force != nullptr && std::strcmp(force, "1") == 0) return false;
    static bool gpu_failed = false;   // one failed try disables the lane for the process
    if (gpu_failed || !gpu_available()) return false;
    try {
        out_fb = raster_gpu(detail::ctx_of<detail::Context>(), prims, bins, params);
        return true;
    } catch (const std::exception& e) {
        gpu_failed = true;            // the one-time notice — later renders skip the lane
        std::fprintf(stderr,
                     "cheatah-plot: GPU raster failed (%s) — falling back to the CPU "
                     "reference rasterizer\n",
                     e.what());
        return false;
    }
}

}  // namespace cheatah::plot::renderer
