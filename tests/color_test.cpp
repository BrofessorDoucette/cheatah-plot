// Unit tests for plot.color — colours, palettes, colormaps. Branch-exhaustive over the generated
// plot/color/color.hpp (every named-colour return, both clamp ends, every anchor-table edge) so the
// QA gate's coverage reports 100% lines + functions. The .purr systest (systests/test_color.purr)
// checks the same surface through the cheatah runtime.
#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "plot/plot.hpp"

namespace c = cheatah::color;

TEST(Color, RgbRgbaAndAutoSentinel) {
    auto steel = c::rgb(0.2, 0.4, 0.8);
    EXPECT_DOUBLE_EQ(steel.r, 0.2);
    EXPECT_DOUBLE_EQ(steel.g, 0.4);
    EXPECT_DOUBLE_EQ(steel.b, 0.8);
    EXPECT_DOUBLE_EQ(steel.a, 1.0);
    EXPECT_FALSE(c::is_auto(steel));

    auto half = c::rgba(1.0, 0.0, 0.0, 0.5);
    EXPECT_DOUBLE_EQ(half.a, 0.5);
    EXPECT_FALSE(c::is_auto(half));

    auto sentinel = c::auto_color();
    EXPECT_DOUBLE_EQ(sentinel.a, 0.0);
    EXPECT_TRUE(c::is_auto(sentinel));
}

TEST(Color, LerpClampsAndBlends) {
    auto black = c::rgb(0.0, 0.0, 0.0);
    auto white = c::rgb(1.0, 1.0, 1.0);
    auto mid = c::lerp(black, white, 0.5);
    EXPECT_DOUBLE_EQ(mid.r, 0.5);
    EXPECT_DOUBLE_EQ(mid.g, 0.5);
    EXPECT_DOUBLE_EQ(mid.b, 0.5);
    EXPECT_DOUBLE_EQ(mid.a, 1.0);

    auto below = c::lerp(black, white, -3.0);   // clamps to c0
    EXPECT_DOUBLE_EQ(below.r, 0.0);
    auto above = c::lerp(black, white, 7.0);    // clamps to c1
    EXPECT_DOUBLE_EQ(above.r, 1.0);
}

TEST(Color, SampleAnchorTableEdges) {
    std::vector<c::Color> empty;
    auto fallback = c::sample(empty, 0.5);      // empty -> opaque black
    EXPECT_DOUBLE_EQ(fallback.r, 0.0);
    EXPECT_DOUBLE_EQ(fallback.a, 1.0);

    std::vector<c::Color> one{c::rgb(0.3, 0.6, 0.9)};
    auto single = c::sample(one, 0.7);          // one anchor -> that anchor
    EXPECT_DOUBLE_EQ(single.g, 0.6);

    std::vector<c::Color> two{c::rgb(0.0, 0.0, 0.0), c::rgb(1.0, 1.0, 1.0)};
    auto mid = c::sample(two, 0.5);             // interior blend
    EXPECT_DOUBLE_EQ(mid.r, 0.5);
    auto lo = c::sample(two, -1.0);             // clamp low
    EXPECT_DOUBLE_EQ(lo.r, 0.0);
    auto hi = c::sample(two, 2.0);              // clamp high -> the i >= n-1 top branch
    EXPECT_DOUBLE_EQ(hi.r, 1.0);
}

TEST(Color, ColormapsHitTheirAnchors) {
    auto v0 = c::viridis(0.0);                  // dark violet end
    EXPECT_NEAR(v0.r, 0.267, 1e-9);
    auto v1 = c::viridis(1.0);                  // yellow end
    EXPECT_NEAR(v1.g, 0.906, 1e-9);
    auto vm = c::viridis(0.5);                  // teal middle anchor (n=5 -> exact)
    EXPECT_NEAR(vm.b, 0.549, 1e-9);

    auto m0 = c::magma(0.0);
    EXPECT_NEAR(m0.b, 0.016, 1e-9);
    auto m1 = c::magma(1.0);
    EXPECT_NEAR(m1.r, 0.988, 1e-9);

    auto w0 = c::coolwarm(0.0);
    EXPECT_NEAR(w0.b, 0.754, 1e-9);
    auto w1 = c::coolwarm(1.0);
    EXPECT_NEAR(w1.r, 0.706, 1e-9);
    auto wm = c::coolwarm(0.5);                 // the pale-grey midpoint anchor
    EXPECT_NEAR(wm.r, 0.865, 1e-9);
}

TEST(Color, PalettesCycleAndDarken) {
    auto base = c::tab10();
    ASSERT_EQ(base.size(), 10u);
    EXPECT_NEAR(base[0].b, 0.706, 1e-9);        // tab10 blue
    EXPECT_NEAR(base[9].g, 0.745, 1e-9);        // tab10 cyan

    auto by_name = c::palette(std::string("tab10"));
    ASSERT_EQ(by_name.size(), 10u);
    EXPECT_NEAR(by_name[0].r, base[0].r, 1e-12);

    auto dark = c::palette(std::string("dark"));
    ASSERT_EQ(dark.size(), 10u);
    EXPECT_NEAR(dark[0].r, base[0].r * 0.72, 1e-9);

    auto unknown = c::palette(std::string("no-such-palette"));  // unknown -> tab10
    ASSERT_EQ(unknown.size(), 10u);
    EXPECT_NEAR(unknown[3].r, base[3].r, 1e-12);
}

TEST(Color, EveryNamedColorResolves) {
    // Every named return line must run for the 100% line gate — the full table, plus unknown.
    struct Row { const char* name; double r, g, b; };
    const Row rows[] = {
        {"white", 1.0, 1.0, 1.0},        {"red", 1.0, 0.0, 0.0},
        {"green", 0.0, 0.502, 0.0},      {"blue", 0.0, 0.0, 1.0},
        {"orange", 1.0, 0.647, 0.0},     {"purple", 0.502, 0.0, 0.502},
        {"brown", 0.647, 0.165, 0.165},  {"pink", 1.0, 0.753, 0.796},
        {"gray", 0.502, 0.502, 0.502},   {"cyan", 0.0, 1.0, 1.0},
        {"magenta", 1.0, 0.0, 1.0},      {"gold", 1.0, 0.843, 0.0},
        {"teal", 0.0, 0.502, 0.502},     {"steelblue", 0.275, 0.510, 0.706},
        {"crimson", 0.863, 0.078, 0.235},{"forestgreen", 0.133, 0.545, 0.133},
        {"darkorange", 1.0, 0.549, 0.0}, {"royalblue", 0.255, 0.412, 0.882},
        {"tomato", 1.0, 0.388, 0.278},   {"slategray", 0.439, 0.502, 0.565},
    };
    for (const auto& row : rows) {
        auto got = c::named(std::string(row.name));
        EXPECT_NEAR(got.r, row.r, 1e-9) << row.name;
        EXPECT_NEAR(got.g, row.g, 1e-9) << row.name;
        EXPECT_NEAR(got.b, row.b, 1e-9) << row.name;
        EXPECT_DOUBLE_EQ(got.a, 1.0) << row.name;
    }
    auto unknown = c::named(std::string("no-such-color"));   // unknown -> opaque black
    EXPECT_DOUBLE_EQ(unknown.r, 0.0);
    EXPECT_DOUBLE_EQ(unknown.g, 0.0);
    EXPECT_DOUBLE_EQ(unknown.b, 0.0);
}

TEST(Color, EmittedHelpers) {
    auto steel = c::rgb(0.2, 0.4, 0.8);

    std::ostringstream os1;
    os1 << steel;
    EXPECT_NE(os1.str().find("Color"), std::string::npos);

    std::ostringstream os2;
    steel.cheatah_pretty_print(os2, 0);
    EXPECT_NE(os2.str().find("Color"), std::string::npos);

    EXPECT_STREQ(c::module_abi(), "color");
}
