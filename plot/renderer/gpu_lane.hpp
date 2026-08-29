#pragma once

/**
 * @file gpu_lane.hpp
 * @brief The plot kernels on cheatah-gpu-linalg's device context: where the compiled kernels
 *        live, the emulated-Metal stand-ins, and the lane accessor the render path dispatches on.
 *
 * cheatah-plot owns NO device context. The renderer dispatches through cheatah-gpu-linalg's
 * `detail::Context` — the one compute layer the cheatah stdlib extensions share (linear algebra,
 * plotting) — reached by @ref detail::lane. The backend is that layer's platform default (Metal
 * on Apple, Vulkan elsewhere; `CHEATAH_GPU_LINALG_VULKAN` / `CHEATAH_GPU_LINALG_METAL` pin a
 * lane for the test matrix) and its device knobs apply unchanged (`CHEATAH_GPU_LINALG_VK_DEVICE`
 * picks the Vulkan device by name substring or index).
 *
 * What stays plot's: the two kernels (shaders/plot.slang, compiled per backend into ONE
 * directory — `CHEATAH_PLOT_SHADER_DIR`, baked at build time, the same-named environment
 * variable overriding at runtime) and, off Apple, the C++ stand-ins the software-emulated Metal
 * device runs in their place. A kernel is handed to the context by DIRECTORY-QUALIFIED name, so
 * the linalg kernels the same context serves never collide with these.
 *
 * Included by render_gpu.hpp under `CHEATAH_PLOT_GPU` only; a CPU-only build never sees it.
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "cheatah_gpu_linalg/context.hpp"

#include "kernels.hpp"
#include "raster_cpu.hpp"

namespace cheatah::plot::renderer {

/// The compute layer the renderer dispatches on.
namespace gl = cheatah::gpu::linalg;

/// The clear kernel's entry name — `plot_clear` in shaders/plot.slang (buffers: fb, params).
inline constexpr const char* kClearKernel = "plot_clear";
/// The raster kernel's entry name — `plot_raster` in shaders/plot.slang (buffers: prims,
/// tile_offsets, tile_prims, fb, params).
inline constexpr const char* kRasterKernel = "plot_raster";

/// Both kernels are `[numthreads(16, 16, 1)]` — the tile edge ON PURPOSE (one workgroup walks
/// one tile bin) — and cheatah-gpu-linalg dimensions every 2-D dispatch with ITS kLocal2d. The
/// two must agree or the grid the context launches under-covers the framebuffer.
static_assert(gl::kernels::kLocal2d == kTile,
              "plot.slang's numthreads must equal the workgroup edge cheatah-gpu-linalg "
              "dispatches with");

namespace detail {

/// The directory holding plot's compiled kernels — `plot_clear.spv` / `plot_raster.spv` for the
/// Vulkan lane, the `.metal` sources for Apple — as the build baked it (`CHEATAH_PLOT_SHADER_DIR`),
/// the same-named environment variable overriding it at runtime (how a test relocates the blobs).
/// @return The directory, without a trailing slash; "" when the build baked none and the
///         environment names none (the context then reports the missing kernel on first dispatch).
/// @complexity O(1) after the first call (cached).
/// @alloc one cached string on the first call. @gpualloc none.
/// @test plot:gpu_raster
inline const std::string& shader_dir() {
    static const std::string dir = [] {
        const char* env = std::getenv("CHEATAH_PLOT_SHADER_DIR");
        if (env != nullptr && env[0] != '\0') return std::string(env);
#if defined(CHEATAH_PLOT_SHADER_DIR)
        return std::string(CHEATAH_PLOT_SHADER_DIR);
#else
        return std::string();
#endif
    }();
    return dir;
}

/// A plot kernel's directory-qualified name: what cheatah-gpu-linalg's context resolves — it
/// appends its lane's extension (`.spv` / `.metal`) and, on the emulated Metal device, looks the
/// basename up among the registered stand-ins.
/// @param name The entry name (@ref kClearKernel / @ref kRasterKernel).
/// @return `<shader_dir>/<name>`.
/// @complexity O(len). @alloc the returned string. @gpualloc none.
/// @test plot:gpu_raster
inline std::string qualified(const char* name) { return shader_dir() + "/" + name; }

#if (defined(CHEATAH_GPU_LINALG_METAL) || defined(CHEATAH_GPU_BACKEND_METAL)) && !defined(__APPLE__)
// This translation unit's lane is cheatah-gpu's software-emulated Metal device (the condition
// is context.hpp's own lane rule, minus Apple): it runs the C++ stand-ins below in place of
// compiled kernels, keyed by the kernel's basename.

namespace emu = cheatah::gpu::metal::emulated;

/// CPU stand-in for `plot_clear` on the emulated Metal device: writes params.clear into every
/// framebuffer pixel, exactly like the kernel's guarded one-thread-per-pixel store.
/// @param b The bound buffer pointers in kernel binding order: {fb, params}.
/// @param n The binding count (no-op when fewer than expected).
/// @param shape The dispatch thread grid (its width/height bound the loops).
/// @complexity O(pixels).
/// @alloc none — writes in place through the bound buffers. @gpualloc none.
/// @test plot:gpu_raster
inline void plot_clear_emulated(void** b, unsigned n, const emu::DispatchShape& shape) {
    if (n < 2 || b[0] == nullptr || b[1] == nullptr) return;
    std::uint32_t* fb = static_cast<std::uint32_t*>(b[0]);
    const std::uint32_t* params = static_cast<const std::uint32_t*>(b[1]);
    const std::uint32_t width = params[0], height = params[1], clear = params[3];
    for (unsigned long y = 0; y < shape.threads.height && y < height; ++y)
        for (unsigned long x = 0; x < shape.threads.width && x < width; ++x)
            fb[static_cast<std::size_t>(y) * width + x] = clear;
}

/// CPU stand-in for `plot_raster` on the emulated Metal device: loops the pixels calling the
/// reference rasterizer's OWN detail::coverage / detail::quantize_cov / detail::blend_over, so
/// this lane is bit-exact with @ref raster_cpu by construction — it IS the same code.
/// @param b The bound buffer pointers in kernel binding order:
///          {prims, tile_offsets, tile_prims, fb, params}.
/// @param n The binding count (no-op when fewer than expected).
/// @param shape The dispatch thread grid (its width/height bound the loops).
/// @complexity O(pixels + Σ per-tile prim work).
/// @alloc none — writes in place through the bound buffers. @gpualloc none.
/// @test plot:gpu_raster
inline void plot_raster_emulated(void** b, unsigned n, const emu::DispatchShape& shape) {
    if (n < 5) return;
    for (unsigned i = 0; i < 5; ++i)
        if (b[i] == nullptr) return;
    const Prim* prims = static_cast<const Prim*>(b[0]);
    const std::uint32_t* offsets = static_cast<const std::uint32_t*>(b[1]);
    const std::uint32_t* indices = static_cast<const std::uint32_t*>(b[2]);
    std::uint32_t* fb = static_cast<std::uint32_t*>(b[3]);
    const std::uint32_t* params = static_cast<const std::uint32_t*>(b[4]);
    const std::uint32_t width = params[0], height = params[1], tiles_x = params[2];
    for (unsigned long y = 0; y < shape.threads.height && y < height; ++y) {
        for (unsigned long x = 0; x < shape.threads.width && x < width; ++x) {
            const std::size_t tile =
                static_cast<std::size_t>(y / kTile) * tiles_x + (x / kTile);
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            std::uint32_t pixel = params[3];
            for (std::uint32_t k = offsets[tile]; k < offsets[tile + 1]; ++k) {
                const Prim& p = prims[indices[k]];
                const std::uint32_t cov8 = quantize_cov(coverage(p, px, py));
                if (cov8 != 0u) pixel = blend_over(pixel, p.rgba, cov8);
            }
            fb[static_cast<std::size_t>(y) * width + x] = pixel;
        }
    }
}

/// Register both raster stand-ins with the software Metal device, keyed by kernel name — the
/// same registration mechanism cheatah-gpu-linalg uses for its own stand-ins. Idempotent.
/// @complexity O(1). @alloc none. @gpualloc none.
/// @test plot:gpu_raster
inline void register_emulated_kernels() {
    emu::register_kernel(kClearKernel, &plot_clear_emulated);
    emu::register_kernel(kRasterKernel, &plot_raster_emulated);
}

#endif  // emulated Metal lane

/// The context the render path dispatches on — cheatah-gpu-linalg's process-wide context, with
/// plot's stand-ins registered ahead of the first dispatch on the emulated-Metal lane. Throws
/// what the context's bring-up throws; `gl::available()` is the never-throwing probe.
/// @return The live context.
/// @complexity O(1) after the first call.
/// @alloc none. @gpualloc the context's bring-up on the first call.
/// @test plot:gpu_raster
inline gl::detail::Context& lane() {
#if (defined(CHEATAH_GPU_LINALG_METAL) || defined(CHEATAH_GPU_BACKEND_METAL)) && !defined(__APPLE__)
    static const bool registered = [] {
        register_emulated_kernels();
        return true;
    }();
    static_cast<void>(registered);
#endif
    return gl::detail::ctx();
}

}  // namespace detail

}  // namespace cheatah::plot::renderer
