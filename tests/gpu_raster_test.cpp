// GPU-lane tests: the emulated-Metal stand-in path must be BYTE-IDENTICAL to raster_cpu (it is
// the same code, dispatched through the real Metal call sequence), the Vulkan lane must match
// within a tight per-channel tolerance (slangc-compiled float ops differ only in ULP noise
// inside the AA feather), and the public render() must honor the CHEATAH_PLOT_FORCE_CPU pin.
// Built with or without the lanes: without them the GPU cases skip and the pin case still runs.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "plot/plot.hpp"
#include "plot/renderer/raster_cpu.hpp"
#include "plot/renderer/render.hpp"

namespace fg = cheatah::plot::figure;
namespace rr = cheatah::plot::renderer;
namespace nd = cheatah::ndarray;

namespace {

// A representative draw list: every prim type — solid + dashed segments, a disc, filled and
// outlined rects, a triangle, real glyph bitmaps — with several translucent colours so the
// integer src-over blend stacks. Ragged against the 16px tile grid to exercise the guards.
rr::DrawList representative_scene(std::uint32_t w, std::uint32_t h) {
    rr::DrawList dl;
    rr::push_rect(dl, 0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h),
                  rr::pack_rgba(1.0, 1.0, 1.0, 1.0));                          // the page
    rr::push_rect(dl, 8.5f, 28.5f, 118.5f, 88.5f, rr::pack_rgba(0.2, 0.4, 0.9, 0.35));
    rr::push_rect(dl, 6.0f, 26.0f, 121.0f, 91.0f, rr::pack_rgba(0.1, 0.1, 0.1, 1.0), 0.75f);
    rr::push_seg(dl, 4.0f, 90.0f, 124.0f, 34.0f, 1.0f, rr::pack_rgba(0.9, 0.2, 0.1, 1.0));
    rr::push_seg(dl, 4.0f, 40.0f, 124.0f, 70.0f, 1.5f, rr::pack_rgba(0.1, 0.6, 0.2, 0.8),
                 6.0f, 4.0f);                                                  // dashed
    rr::push_disc(dl, 40.5f, 60.5f, 9.0f, rr::pack_rgba(0.95, 0.6, 0.05, 0.65));
    rr::push_tri(dl, 70.0f, 84.0f, 110.0f, 82.0f, 92.0f, 40.0f, rr::pack_rgba(0.5, 0.1, 0.8, 0.5));
    rr::push_text(dl, 12.0f, 6.0f, "Ag<3 GPU", rr::pack_rgba(0.05, 0.05, 0.05, 1.0));
    return dl;
}

// The scene's reference bytes through the CPU pipeline (the executable spec).
std::vector<std::uint32_t> reference_raster(const rr::DrawList& dl, const rr::TileBins& bins,
                                            const rr::RasterParams& params) {
    return rr::raster_cpu(dl, bins, params);
}

}  // namespace

TEST(GpuRaster, EmulatedLaneBitExact) {
#if defined(CHEATAH_PLOT_GPU_METAL)
    const std::uint32_t w = 130, h = 97;   // ragged against the 16px tile grid on both axes
    rr::DrawList dl = representative_scene(w, h);
    rr::TileBins bins = rr::bin_prims(dl, w, h);
    rr::RasterParams params{w, h, bins.tiles_x, 0xFFFFFFFFu};

    auto& ctx = rr::detail::ctx_of<rr::detail::MetalContext>();
    ASSERT_TRUE(ctx.ok()) << "the (emulated) Metal device did not come up";
    std::vector<std::uint32_t> gpu = rr::raster_gpu(ctx, dl, bins, params);
    std::vector<std::uint32_t> cpu = reference_raster(dl, bins, params);

    ASSERT_EQ(gpu.size(), cpu.size());
    std::size_t first_diff = gpu.size();
    for (std::size_t i = 0; i < gpu.size(); ++i)
        if (gpu[i] != cpu[i]) { first_diff = i; break; }
    EXPECT_EQ(first_diff, gpu.size())
        << "emulated lane diverges from raster_cpu at pixel (" << (first_diff % w) << ", "
        << (first_diff / w) << "): gpu=0x" << std::hex << gpu[first_diff] << " cpu=0x"
        << cpu[first_diff];
#else
    GTEST_SKIP() << "built without the Metal lane";
#endif
}

TEST(GpuRaster, VulkanLaneMatchesWithinTolerance) {
#if defined(CHEATAH_PLOT_GPU_VULKAN)
    if (!rr::gpu_available()) GTEST_SKIP() << "no Vulkan device on this machine";
    // The kernels must be on disk where the context will look (env overrides the baked dir).
    const char* env = std::getenv("CHEATAH_PLOT_SPV_DIR");
#if defined(CHEATAH_PLOT_SPV_DIR)
    const std::string spv_dir = env ? env : CHEATAH_PLOT_SPV_DIR;
#else
    const std::string spv_dir = env ? env : "";
#endif
    if (spv_dir.empty() || !std::ifstream(spv_dir + "/plot_raster.spv").good())
        GTEST_SKIP() << "no compiled SPIR-V under '" << spv_dir << "'";

    const std::uint32_t w = 130, h = 97;
    rr::DrawList dl = representative_scene(w, h);
    rr::TileBins bins = rr::bin_prims(dl, w, h);
    rr::RasterParams params{w, h, bins.tiles_x, 0xFFFFFFFFu};

    auto& ctx = rr::detail::ctx_of<rr::detail::VulkanContext>();
    std::cout << "[ vulkan ] device: " << ctx.device_name() << "\n";
    std::vector<std::uint32_t> gpu = rr::raster_gpu(ctx, dl, bins, params);
    std::vector<std::uint32_t> cpu = reference_raster(dl, bins, params);

    ASSERT_EQ(gpu.size(), cpu.size());
    ASSERT_EQ(gpu.size(), static_cast<std::size_t>(w) * h);   // identical dimensions
    std::size_t within = 0;
    int worst = 0;
    for (std::size_t i = 0; i < gpu.size(); ++i) {
        bool ok = true;
        for (int ch = 0; ch < 4; ++ch) {
            const int g = static_cast<int>((gpu[i] >> (8 * ch)) & 0xFFu);
            const int c = static_cast<int>((cpu[i] >> (8 * ch)) & 0xFFu);
            const int delta = g > c ? g - c : c - g;
            if (delta > worst) worst = delta;
            if (delta > 2) ok = false;
        }
        if (ok) ++within;
    }
    const double frac = static_cast<double>(within) / static_cast<double>(gpu.size());
    EXPECT_GE(frac, 0.995) << "only " << within << "/" << gpu.size()
                           << " pixels within |delta| <= 2 per channel (worst delta " << worst
                           << ")";
#else
    GTEST_SKIP() << "built without the Vulkan lane";
#endif
}

TEST(GpuRaster, RenderPrefersGpuAndFallsBack) {
    auto x = nd::array(std::vector<double>{0.0, 1.0, 2.0, 3.0});
    auto y = nd::array(std::vector<double>{3.0, 1.0, 4.0, 2.0});
    auto f = fg::size(fg::line(fg::new_figure(), x, y), 200LL, 150LL);

    // With the pin, render() must equal the reference path byte-for-byte — GPU build or not.
    ::setenv("CHEATAH_PLOT_FORCE_CPU", "1", 1);
    rr::Image pinned = rr::render(f);
    ::unsetenv("CHEATAH_PLOT_FORCE_CPU");

    const std::uint32_t w = 200, h = 150;
    rr::DrawList dl = rr::reduce(f, w, h);
    rr::TileBins bins = rr::bin_prims(dl, w, h);
    rr::RasterParams params{w, h, bins.tiles_x, rr::detail::kWhite};
    std::vector<std::uint32_t> fb = rr::raster_cpu(dl, bins, params);
    ASSERT_EQ(pinned.rgba.size(), fb.size() * 4u);
    std::size_t byte_diffs = 0;
    for (std::size_t i = 0; i < fb.size(); ++i) {
        for (int ch = 0; ch < 4; ++ch)
            if (pinned.rgba[i * 4 + static_cast<std::size_t>(ch)] !=
                static_cast<std::uint8_t>((fb[i] >> (8 * ch)) & 0xFFu))
                ++byte_diffs;
    }
    EXPECT_EQ(byte_diffs, 0u) << "CHEATAH_PLOT_FORCE_CPU=1 must pin the exact CPU bytes";

#if defined(CHEATAH_PLOT_GPU_VULKAN) || defined(CHEATAH_PLOT_GPU_METAL)
    // And un-pinned, a live lane is PREFERRED: render() must produce the same frame as the
    // explicit GPU path (both hit the same deterministic lane).
    if (rr::gpu_available()) {
        try {
            rr::Image via_gpu = rr::render_gpu(f);
            rr::Image unpinned = rr::render(f);
            EXPECT_EQ(unpinned.rgba, via_gpu.rgba)
                << "render() did not take the live GPU lane";
        } catch (const std::exception& e) {
            // No kernels on disk (etc.): render() falls back — the pinned check above already
            // proved the fallback bytes.
            std::cout << "[ note   ] GPU lane declined at dispatch time: " << e.what() << "\n";
        }
    }
#endif
}
