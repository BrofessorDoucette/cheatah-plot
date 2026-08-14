// Google Benchmark suite for the renderer's hot paths: drawlist build (reduce), tile binning,
// the reference raster's fill rate, PNG encoding, and the full figure->PNG pipeline.
#include <benchmark/benchmark.h>

#include <vector>

#include "plot/plot.hpp"

namespace fg = cheatah::plot::figure;
namespace rr = cheatah::plot::renderer;
namespace nd = cheatah::ndarray;

namespace {

fg::Figure demo_figure(long long points) {
    std::vector<double> xs(static_cast<std::size_t>(points)), ys(xs.size());
    for (std::size_t i = 0; i < xs.size(); ++i) {
        xs[i] = static_cast<double>(i);
        ys[i] = static_cast<double>((i * 37) % 101);
    }
    auto x = nd::array(xs);
    auto y = nd::array(ys);
    auto f = fg::line(fg::new_figure(), x, y);
    f = fg::scatter(f, x, y);
    return fg::size(f, 800LL, 600LL);
}

void BM_ReduceFigure(benchmark::State& state) {
    auto f = demo_figure(state.range(0));
    for (auto _ : state) {
        auto dl = rr::reduce(f, 800, 600);
        benchmark::DoNotOptimize(dl.data());
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_ReduceFigure)->Arg(1000)->Arg(10000);

void BM_BinPrims(benchmark::State& state) {
    auto f = demo_figure(state.range(0));
    auto dl = rr::reduce(f, 800, 600);
    for (auto _ : state) {
        auto bins = rr::bin_prims(dl, 800, 600);
        benchmark::DoNotOptimize(bins.tile_prims.data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<long long>(dl.size()));
}
BENCHMARK(BM_BinPrims)->Arg(1000)->Arg(10000);

void BM_RasterFillRate(benchmark::State& state) {
    auto f = demo_figure(5000);
    auto dl = rr::reduce(f, 800, 600);
    auto bins = rr::bin_prims(dl, 800, 600);
    rr::RasterParams params{800, 600, bins.tiles_x, 0xFFFFFFFFu};
    for (auto _ : state) {
        auto fb = rr::raster_cpu(dl, bins, params);
        benchmark::DoNotOptimize(fb.data());
    }
    state.SetItemsProcessed(state.iterations() * 800 * 600);   // pixels/s
}
BENCHMARK(BM_RasterFillRate);

void BM_EncodePng(benchmark::State& state) {
    auto f = demo_figure(2000);
    rr::Image img = rr::render(f);
    for (auto _ : state) {
        auto bytes = rr::encode_png(img);
        benchmark::DoNotOptimize(bytes.data());
    }
    state.SetBytesProcessed(state.iterations() * static_cast<long long>(img.rgba.size()));
}
BENCHMARK(BM_EncodePng);

void BM_FigureToPngPipeline(benchmark::State& state) {
    auto f = demo_figure(2000);
    for (auto _ : state) {
        rr::Image img = rr::render(f);
        auto bytes = rr::encode_png(img);
        benchmark::DoNotOptimize(bytes.data());
    }
}
BENCHMARK(BM_FigureToPngPipeline);

}  // namespace

BENCHMARK_MAIN();
