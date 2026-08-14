// Unit tests for plot.figure — the figure model and its fluent free functions. Branch-exhaustive
// over the generated plot/figure/figure.hpp for the 100% coverage gate: every fluent setter, every
// clamp arm, copy semantics (the input figure never mutates), and the stats-backed hist. The .purr
// systest (systests/test_figure.purr) checks the same surface through the cheatah runtime.
#include <gtest/gtest.h>

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "plot/plot.hpp"

namespace fg = cheatah::figure;
namespace nd = cheatah::ndarray;

TEST(Figure, ScalesAndDefaultAxis) {
    EXPECT_EQ(fg::linear().kind, "linear");
    EXPECT_EQ(fg::log_scale().kind, "log");

    auto ax = fg::default_axis();
    EXPECT_EQ(ax.label, "");
    EXPECT_EQ(ax.scale.kind, "linear");
    EXPECT_TRUE(std::isnan(ax.lo));
    EXPECT_TRUE(std::isnan(ax.hi));
    EXPECT_EQ(ax.ticks, 6);
}

TEST(Figure, GridBuildsAndClamps) {
    auto f = fg::grid(2LL, 3LL);
    EXPECT_EQ(f.rows, 2);
    EXPECT_EQ(f.cols, 3);
    ASSERT_EQ(f.subplots.size(), 6u);
    EXPECT_EQ(f.cur, 0);
    EXPECT_EQ(f.width, 900);
    EXPECT_EQ(f.height, 600);
    ASSERT_EQ(f.palette.size(), 10u);

    auto clamped = fg::grid(0LL, -2LL);             // both clamps -> 1x1
    EXPECT_EQ(clamped.rows, 1);
    EXPECT_EQ(clamped.cols, 1);
    ASSERT_EQ(clamped.subplots.size(), 1u);

    auto single = fg::new_figure();
    EXPECT_EQ(single.rows, 1);
    ASSERT_EQ(single.subplots.size(), 1u);
}

TEST(Figure, SubplotTargetsAndClamps) {
    auto f = fg::grid(2LL, 2LL);
    auto g = fg::subplot(f, 1LL, 1LL);
    EXPECT_EQ(g.cur, 3);
    EXPECT_EQ(f.cur, 0);                        // the input is never mutated

    EXPECT_EQ(fg::subplot(f, -1LL, 0LL).cur, 0);    // row clamp low
    EXPECT_EQ(fg::subplot(f, 5LL, 0LL).cur, 2);     // row clamp high
    EXPECT_EQ(fg::subplot(f, 0LL, -3LL).cur, 0);    // col clamp low
    EXPECT_EQ(fg::subplot(f, 0LL, 9LL).cur, 1);     // col clamp high
}

TEST(Figure, AddAppendsToTheTargetedCellOnly) {
    auto x = nd::array(std::vector<double>{0.0, 1.0});
    auto y = nd::array(std::vector<double>{2.0, 3.0});
    auto f = fg::grid(1LL, 2LL);
    auto g = fg::add(fg::subplot(f, 0LL, 1LL), cheatah::series::line(x, y));
    EXPECT_EQ(f.subplots[1].series.size(), 0u); // original untouched
    ASSERT_EQ(g.subplots[1].series.size(), 1u);
    EXPECT_EQ(g.subplots[0].series.size(), 0u);
    EXPECT_EQ(g.subplots[1].series[0].kind, "line");
}

TEST(Figure, FluentMarksSetTheirKinds) {
    auto x = nd::array(std::vector<double>{0.0, 1.0, 2.0});
    auto y = nd::array(std::vector<double>{1.0, 2.0, 3.0});
    auto lo = nd::array(std::vector<double>{0.5, 1.5, 2.5});
    auto hi = nd::array(std::vector<double>{1.5, 2.5, 3.5});
    auto flat = nd::array(std::vector<double>{1.0, 2.0, 3.0, 4.0});
    auto z = nd::reshape(flat, {2, 2});

    auto f = fg::new_figure();
    auto g = fg::line(f, x, y);
    g = fg::scatter(g, x, y);
    g = fg::bar(g, x, y);
    g = fg::area(g, x, y);
    g = fg::step(g, x, y);
    g = fg::errorbar(g, x, y, lo, hi);
    g = fg::heatmap(g, z);
    ASSERT_EQ(g.subplots[0].series.size(), 7u);
    EXPECT_EQ(g.subplots[0].series[0].kind, "line");
    EXPECT_EQ(g.subplots[0].series[1].kind, "scatter");
    EXPECT_EQ(g.subplots[0].series[2].kind, "bar");
    EXPECT_EQ(g.subplots[0].series[3].kind, "area");
    EXPECT_EQ(g.subplots[0].series[4].kind, "step");
    EXPECT_EQ(g.subplots[0].series[5].kind, "errorbar");
    EXPECT_EQ(g.subplots[0].series[6].kind, "heatmap");
    EXPECT_EQ(f.subplots[0].series.size(), 0u); // the seed figure never mutates
}

TEST(Figure, HistBinsThroughStats) {
    auto data = nd::array(std::vector<double>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
    auto g = fg::hist(fg::new_figure(), data, 5LL);
    ASSERT_EQ(g.subplots[0].series.size(), 1u);
    auto& s = g.subplots[0].series[0];          // non-const: ndarray indexing is non-const
    EXPECT_EQ(s.kind, "bar");
    EXPECT_DOUBLE_EQ(s.width, 1.0);             // touching bars, histogram convention
    ASSERT_EQ(nd::size_of(s.x), 5);
    double total = 0.0;
    for (int i = 0; i < 5; ++i) total += s.y[i];
    EXPECT_DOUBLE_EQ(total, 10.0);
}

TEST(Figure, TextSettersTargetTheCurrentCell) {
    auto f = fg::new_figure();
    auto g = fg::title(f, std::string("growth"));
    g = fg::xlabel(g, std::string("time"));
    g = fg::ylabel(g, std::string("value"));
    EXPECT_EQ(g.subplots[0].title, "growth");
    EXPECT_EQ(g.subplots[0].x.label, "time");
    EXPECT_EQ(g.subplots[0].y.label, "value");
    EXPECT_EQ(f.subplots[0].title, "");         // original untouched
}

TEST(Figure, LimitsScalesLegend) {
    auto f = fg::new_figure();
    auto g = fg::xlim(f, 0.0, 10.0);
    g = fg::ylim(g, 1.0, 1000.0);
    g = fg::xscale(g, fg::linear());
    g = fg::yscale(g, fg::log_scale());
    g = fg::legend(g, true);
    EXPECT_DOUBLE_EQ(g.subplots[0].x.lo, 0.0);
    EXPECT_DOUBLE_EQ(g.subplots[0].x.hi, 10.0);
    EXPECT_DOUBLE_EQ(g.subplots[0].y.lo, 1.0);
    EXPECT_DOUBLE_EQ(g.subplots[0].y.hi, 1000.0);
    EXPECT_EQ(g.subplots[0].x.scale.kind, "linear");
    EXPECT_EQ(g.subplots[0].y.scale.kind, "log");
    EXPECT_TRUE(g.subplots[0].legend);
    EXPECT_TRUE(std::isnan(f.subplots[0].x.lo));  // original still auto
}

TEST(Figure, SizeSetsAndClamps) {
    auto f = fg::new_figure();
    auto g = fg::size(f, 1280LL, 720LL);
    EXPECT_EQ(g.width, 1280);
    EXPECT_EQ(g.height, 720);

    auto tiny = fg::size(f, 1LL, -5LL);             // both clamps
    EXPECT_EQ(tiny.width, 64);
    EXPECT_EQ(tiny.height, 64);
}

TEST(Figure, EmittedHelpers) {
    // purrc emits print glue only for structs without list<Struct> fields — here Scale and Axis
    // (Subplot/Figure carry vectors of structs and get none).
    auto ax = fg::default_axis();
    auto sc = fg::linear();

    std::ostringstream os3;
    os3 << ax;
    EXPECT_NE(os3.str().find("Axis"), std::string::npos);
    std::ostringstream os4;
    ax.cheatah_pretty_print(os4, 0);
    EXPECT_NE(os4.str().find("Axis"), std::string::npos);

    std::ostringstream os5;
    os5 << sc;
    EXPECT_NE(os5.str().find("Scale"), std::string::npos);
    std::ostringstream os6;
    sc.cheatah_pretty_print(os6, 0);
    EXPECT_NE(os6.str().find("Scale"), std::string::npos);

    EXPECT_STREQ(fg::module_abi(), "figure");
}
