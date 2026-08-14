// End-to-end renderer tests: a real Figure through reduce → bin → raster → pixels/PNG. These
// assert semantic pixel properties (marks land where the layout puts them, colours resolve,
// log axes transform) rather than golden bytes — the reference path's determinism is covered
// by raster_test; goldens against the GPU lanes come with those lanes.
#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "plot/plot.hpp"

namespace fg = cheatah::plot::figure;
namespace rr = cheatah::plot::renderer;
namespace nd = cheatah::ndarray;

namespace {

// The packed pixel at (x, y) of a rendered image.
std::uint32_t px_at(const rr::Image& img, std::uint32_t x, std::uint32_t y) {
    const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4u;
    return static_cast<std::uint32_t>(img.rgba[i]) |
           (static_cast<std::uint32_t>(img.rgba[i + 1]) << 8) |
           (static_cast<std::uint32_t>(img.rgba[i + 2]) << 16) |
           (static_cast<std::uint32_t>(img.rgba[i + 3]) << 24);
}

// Count pixels that differ from opaque white.
std::size_t inked(const rr::Image& img) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < img.rgba.size(); i += 4)
        if (!(img.rgba[i] == 255 && img.rgba[i + 1] == 255 && img.rgba[i + 2] == 255)) ++n;
    return n;
}

}  // namespace

TEST(Render, LineFigureProducesInkWhereTheDataIs) {
    auto x = nd::array(std::vector<double>{0.0, 1.0, 2.0, 3.0});
    auto y = nd::array(std::vector<double>{0.0, 1.0, 2.0, 3.0});
    auto f = fg::size(fg::line(fg::new_figure(), x, y), 320LL, 240LL);
    rr::Image img = rr::render(f);
    EXPECT_EQ(img.width, 320u);
    EXPECT_EQ(img.height, 240u);
    // The page is white, the axes+grid+line ink a real number of pixels.
    EXPECT_GT(inked(img), 200u);
    EXPECT_LT(inked(img), img.rgba.size() / 4u / 2u);
    // The diagonal's midpoint carries the first palette colour (tab10 blue) near the plot
    // center; sample a small neighborhood to be layout-tolerant.
    bool found_blue = false;
    for (std::uint32_t yy = 90; yy < 150 && !found_blue; ++yy)
        for (std::uint32_t xx = 140; xx < 220 && !found_blue; ++xx) {
            const std::uint32_t p = px_at(img, xx, yy);
            const std::uint32_t r = p & 0xFFu, g = (p >> 8) & 0xFFu, b = (p >> 16) & 0xFFu;
            if (b > 150 && b > r + 40 && g < b) found_blue = true;
        }
    EXPECT_TRUE(found_blue) << "no tab10-blue line ink near the plot center";
}

TEST(Render, MarksRenderAcrossKinds) {
    auto x = nd::array(std::vector<double>{1.0, 2.0, 3.0, 4.0});
    auto y = nd::array(std::vector<double>{3.0, 1.0, 4.0, 2.0});
    auto lo = nd::array(std::vector<double>{2.5, 0.5, 3.5, 1.5});
    auto hi = nd::array(std::vector<double>{3.5, 1.5, 4.5, 2.5});
    auto zf = nd::array(std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0, 6.0});
    auto z = nd::reshape(zf, {2, 3});

    auto f = fg::grid(2LL, 3LL);
    f = fg::scatter(fg::subplot(f, 0LL, 0LL), x, y);
    f = fg::bar(fg::subplot(f, 0LL, 1LL), x, y);
    f = fg::area(fg::subplot(f, 0LL, 2LL), x, y);
    f = fg::errorbar(fg::subplot(f, 1LL, 0LL), x, y, lo, hi);
    f = fg::step(fg::subplot(f, 1LL, 1LL), x, y);
    f = fg::heatmap(fg::subplot(f, 1LL, 2LL), z);
    f = fg::size(f, 660LL, 400LL);
    rr::Image img = rr::render(f);
    // Every cell inks: split the canvas into the 6 cells and demand ink in each.
    for (int cy = 0; cy < 2; ++cy)
        for (int cx = 0; cx < 3; ++cx) {
            std::size_t n = 0;
            for (std::uint32_t yy = static_cast<std::uint32_t>(cy * 200);
                 yy < static_cast<std::uint32_t>((cy + 1) * 200); ++yy)
                for (std::uint32_t xx = static_cast<std::uint32_t>(cx * 220);
                     xx < static_cast<std::uint32_t>((cx + 1) * 220); ++xx) {
                    const std::uint32_t p = px_at(img, xx, yy);
                    if ((p & 0x00FFFFFFu) != 0x00FFFFFFu) ++n;
                }
            EXPECT_GT(n, 100u) << "cell (" << cy << "," << cx << ") drew almost nothing";
        }
}

TEST(Render, LegendTitleAndLabelsInkText) {
    auto x = nd::array(std::vector<double>{0.0, 1.0});
    auto y = nd::array(std::vector<double>{0.0, 1.0});
    auto plain = fg::size(fg::line(fg::new_figure(), x, y), 300LL, 200LL);
    auto titled = fg::title(plain, std::string("growth"));
    titled = fg::legend(titled, true);
    // A labeled series so the legend has an entry.
    auto s = cheatah::series::line(x, y);
    s.label = "run";
    titled = fg::add(titled, s);
    rr::Image with_text = rr::render(titled);
    rr::Image without = rr::render(plain);

    // The title band (above the untitled viewport top) is empty on the plain figure and
    // carries glyph ink on the titled one.
    auto band_ink = [](const rr::Image& img, std::uint32_t y0, std::uint32_t y1) {
        std::size_t n = 0;
        for (std::uint32_t yy = y0; yy < y1; ++yy)
            for (std::uint32_t xx = 0; xx < img.width; ++xx)
                if ((px_at(img, xx, yy) & 0x00FFFFFFu) != 0x00FFFFFFu) ++n;
        return n;
    };
    EXPECT_EQ(band_ink(without, 0, 20), 0u);
    EXPECT_GT(band_ink(with_text, 0, 20), 20u);          // "growth" glyph pixels

    // The legend box adds ink inside the viewport's top-right corner region.
    EXPECT_GT(band_ink(with_text, 46, 90), band_ink(without, 26, 70));
}

TEST(Render, LogAxisMovesTheDecades) {
    auto x = nd::array(std::vector<double>{1.0, 10.0, 100.0});
    auto y = nd::array(std::vector<double>{1.0, 10.0, 100.0});
    auto lin = fg::size(fg::scatter(fg::new_figure(), x, y), 300LL, 200LL);
    auto log = fg::yscale(fg::xscale(lin, fg::log_scale()), fg::log_scale());
    rr::Image a = rr::render(lin);
    rr::Image b = rr::render(log);
    ASSERT_EQ(a.rgba.size(), b.rgba.size());
    EXPECT_NE(a.rgba, b.rgba);   // the transform visibly moves the marks + ticks
}

TEST(Render, SaveWritesARealPng) {
    auto x = nd::array(std::vector<double>{0.0, 1.0, 2.0});
    auto y = nd::array(std::vector<double>{2.0, 0.5, 1.5});
    auto f = fg::size(fg::line(fg::new_figure(), x, y), 200LL, 150LL);
    const std::string path = ::testing::TempDir() + "render_test_line.png";
    rr::save(f, path);
    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in.good());
    unsigned char sig[8] = {0};
    in.read(reinterpret_cast<char*>(sig), 8);
    const unsigned char want[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i) EXPECT_EQ(sig[i], want[i]) << i;
}
