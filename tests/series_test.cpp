// Unit tests for plot.series — every constructor and both arities, branch-exhaustive over the
// generated plot/series/series.hpp for the 100% coverage gate. The .purr systest
// (systests/test_series.purr) checks the same surface through the cheatah runtime.
#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "plot/plot.hpp"


namespace c  = cheatah::color;
namespace se = cheatah::series;
namespace nd = cheatah::ndarray;

TEST(Series, BlankCarriesTheHouseDefaults) {
    auto s = se::blank(std::string("line"));
    EXPECT_EQ(s.kind, "line");
    EXPECT_EQ(nd::size_of(s.x), 0);
    EXPECT_EQ(nd::size_of(s.y), 0);
    EXPECT_EQ(nd::size_of(s.ylo), 0);
    EXPECT_EQ(nd::size_of(s.yhi), 0);
    EXPECT_EQ(nd::size_of(s.z), 0);
    EXPECT_TRUE(s.by.empty());
    EXPECT_TRUE(c::is_auto(s.color));
    EXPECT_DOUBLE_EQ(s.width, 1.5);
    EXPECT_DOUBLE_EQ(s.size, 6.0);
    EXPECT_EQ(nd::size_of(s.dash), 0);
    EXPECT_EQ(s.marker, "");
    EXPECT_EQ(s.label, "");
    EXPECT_TRUE(s.fill);
}

TEST(Series, DashPattern) {
    auto d = se::dash_pattern(6.0, 3.0);
    ASSERT_EQ(nd::size_of(d), 2);
    EXPECT_DOUBLE_EQ(d[0], 6.0);
    EXPECT_DOUBLE_EQ(d[1], 3.0);
}

TEST(Series, LineBothArities) {
    auto x = nd::array(std::vector<double>{0.0, 1.0, 2.0});
    auto y = nd::array(std::vector<double>{1.0, 3.0, 5.0});
    auto s = se::line(x, y);
    EXPECT_EQ(s.kind, "line");
    EXPECT_EQ(nd::size_of(s.x), 3);
    EXPECT_TRUE(c::is_auto(s.color));

    auto dash = se::dash_pattern(4.0, 2.0);
    auto crimson = c::named(std::string("crimson"));   // lvalue: purrc params take T&
    auto styled = se::line_styled(x, y, crimson, 2.5, dash, std::string("trend"));
    EXPECT_NEAR(styled.color.r, 0.863, 1e-9);
    EXPECT_DOUBLE_EQ(styled.width, 2.5);
    ASSERT_EQ(nd::size_of(styled.dash), 2);
    EXPECT_EQ(styled.label, "trend");
}

TEST(Series, ScatterAritiesAndGrouping) {
    auto x = nd::array(std::vector<double>{0.0, 1.0});
    auto y = nd::array(std::vector<double>{2.0, 4.0});
    auto s = se::scatter(x, y);
    EXPECT_EQ(s.kind, "scatter");
    EXPECT_EQ(s.marker, "circle");

    auto ink = c::rgb(0.1, 0.2, 0.3);
    auto styled = se::scatter_styled(x, y, ink, 9.0, std::string("diamond"), std::string("pts"));
    EXPECT_DOUBLE_EQ(styled.size, 9.0);
    EXPECT_EQ(styled.marker, "diamond");
    EXPECT_EQ(styled.label, "pts");

    std::vector<long long> by{0, 1};
    auto grouped = se::scatter_grouped(x, y, by);
    ASSERT_EQ(grouped.by.size(), 2u);
    EXPECT_EQ(grouped.by[1], 1);
}

TEST(Series, BarBothArities) {
    auto x = nd::array(std::vector<double>{1.0, 2.0, 3.0});
    auto y = nd::array(std::vector<double>{4.0, 7.0, 2.0});
    auto s = se::bar(x, y);
    EXPECT_EQ(s.kind, "bar");
    EXPECT_DOUBLE_EQ(s.width, 0.8);
    EXPECT_TRUE(s.fill);

    auto teal = c::named(std::string("teal"));
    auto outline = se::bar_styled(x, y, teal, 0.5, false, std::string("counts"));
    EXPECT_DOUBLE_EQ(outline.width, 0.5);
    EXPECT_FALSE(outline.fill);
    EXPECT_EQ(outline.label, "counts");
}

TEST(Series, AreaStepStemFill) {
    auto x = nd::array(std::vector<double>{0.0, 1.0});
    auto y = nd::array(std::vector<double>{1.0, 2.0});
    auto lo = nd::array(std::vector<double>{0.5, 1.5});
    auto hi = nd::array(std::vector<double>{1.5, 2.5});

    EXPECT_EQ(se::area(x, y).kind, "area");
    EXPECT_EQ(se::step(x, y).kind, "step");

    auto st = se::stem(x, y);
    EXPECT_EQ(st.kind, "stem");
    EXPECT_EQ(st.marker, "circle");

    auto band = se::fill_between(x, lo, hi);
    EXPECT_EQ(band.kind, "fill_between");
    EXPECT_EQ(nd::size_of(band.ylo), 2);
    EXPECT_EQ(nd::size_of(band.yhi), 2);
    EXPECT_EQ(nd::size_of(band.y), 0);
}

TEST(Series, ErrorbarCarriesBothBounds) {
    auto x = nd::array(std::vector<double>{0.0, 1.0});
    auto y = nd::array(std::vector<double>{2.0, 4.0});
    auto lo = nd::array(std::vector<double>{1.5, 3.0});
    auto hi = nd::array(std::vector<double>{2.5, 5.0});
    auto s = se::errorbar(x, y, lo, hi);
    EXPECT_EQ(s.kind, "errorbar");
    EXPECT_DOUBLE_EQ(s.ylo[0], 1.5);
    EXPECT_DOUBLE_EQ(s.yhi[1], 5.0);
}

TEST(Series, HeatmapCarriesTheGrid) {
    auto flat = nd::array(std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0, 6.0});
    auto z = nd::reshape(flat, {2, 3});
    auto s = se::heatmap(z);
    EXPECT_EQ(s.kind, "heatmap");
    EXPECT_EQ(nd::size_of(s.z), 6);
    EXPECT_EQ(nd::size_of(s.x), 0);
}

TEST(Series, EmittedHelpers) {
    // Series carries ndarray fields, so purrc emits no print glue for it — module_abi is the
    // emitted surface to touch here.
    EXPECT_STREQ(se::module_abi(), "series");
}
