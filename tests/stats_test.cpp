// Unit tests for plot.stats — histogram binning, the lstsq fit line, the SEM helper.
// Branch-exhaustive over the generated plot/stats/stats.hpp for the 100% coverage gate:
// both bin clamps, the NaN-led range fallback, NaN skipping, and both fit arms. The .purr
// systest (systests/test_stats.purr) checks the same surface through the cheatah runtime.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "plot/plot.hpp"

namespace st = cheatah::stats;
namespace nd = cheatah::ndarray;

namespace {
const double kNaN = std::numeric_limits<double>::quiet_NaN();
}

TEST(Stats, HistogramBinsUniformData) {
    // 0..9 over 5 bins of the widened range: every bin gets exactly 2.
    auto data = nd::array(std::vector<double>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
    auto h = st::histogram(data, 5LL);
    ASSERT_EQ(nd::size_of(h.edges), 6);
    ASSERT_EQ(nd::size_of(h.centers), 5);
    ASSERT_EQ(nd::size_of(h.counts), 5);
    double total = 0.0;
    for (int i = 0; i < 5; ++i) total += h.counts[i];
    EXPECT_DOUBLE_EQ(total, 10.0);
    EXPECT_DOUBLE_EQ(h.edges[0], 0.0);
    EXPECT_DOUBLE_EQ(h.edges[5], 9.0);
    EXPECT_NEAR(h.centers[0], 0.9, 1e-12);      // lo + 0.5*step, step = 1.8
    // The top value (9.0) folds into the last bin — the k >= b clamp.
    EXPECT_DOUBLE_EQ(h.counts[4], 2.0);
}

TEST(Stats, HistogramClampsBinCountAndSkipsNaN) {
    auto data = nd::array(std::vector<double>{1.0, kNaN, 2.0, kNaN, 3.0});
    auto h = st::histogram(data, 0LL);            // bins < 1 -> clamped to 1
    ASSERT_EQ(nd::size_of(h.counts), 1);
    EXPECT_DOUBLE_EQ(h.counts[0], 3.0);         // the two NaNs never counted
}

TEST(Stats, HistogramNaNLedRangeFallsBack) {
    // First element NaN poisons data_range's scan -> the [0, 1] fallback range; the negative
    // value drives the k < 0 clamp and the large one the k >= b clamp.
    auto data = nd::array(std::vector<double>{kNaN, -5.0, 0.5, 9.0});
    auto h = st::histogram(data, 4LL);
    ASSERT_EQ(nd::size_of(h.counts), 4);
    EXPECT_DOUBLE_EQ(h.edges[0], 0.0);
    EXPECT_DOUBLE_EQ(h.edges[4], 1.0);
    EXPECT_DOUBLE_EQ(h.counts[0], 1.0);         // -5.0 clamped up into bin 0
    EXPECT_DOUBLE_EQ(h.counts[3], 1.0);         // 9.0 clamped down into the last bin
    double total = 0.0;
    for (int i = 0; i < 4; ++i) total += h.counts[i];
    EXPECT_DOUBLE_EQ(total, 3.0);               // the NaN itself never counted
}

TEST(Stats, FitRecoversAnExactLine) {
    auto x = nd::array(std::vector<double>{0.0, 1.0, 2.0, 3.0});
    auto y = nd::array(std::vector<double>{1.0, 3.0, 5.0, 7.0});   // y = 2x + 1 exactly
    auto fitted = st::fit(x, y);
    ASSERT_EQ(nd::size_of(fitted), 4);
    for (int i = 0; i < 4; ++i) EXPECT_NEAR(fitted[i], y[i], 1e-9) << i;

    // Noisy but symmetric around y = x: the lstsq line passes through the middle.
    auto xs = nd::array(std::vector<double>{0.0, 1.0, 2.0, 3.0});
    auto ys = nd::array(std::vector<double>{0.5, 0.5, 2.5, 2.5});
    auto mid = st::fit(xs, ys);
    EXPECT_NEAR(mid[0] + mid[3], ys[0] + ys[3], 1e-9);             // symmetric residuals
}

TEST(Stats, FitDegeneratesToTheData) {
    auto x = nd::array(std::vector<double>{4.0});
    auto y = nd::array(std::vector<double>{2.0});
    auto same = st::fit(x, y);                  // n < 2 -> y unchanged
    ASSERT_EQ(nd::size_of(same), 1);
    EXPECT_DOUBLE_EQ(same[0], 2.0);
}

TEST(Stats, SemMatchesTheHandComputation) {
    auto data = nd::array(std::vector<double>{1.0, 2.0, 3.0, 4.0});
    // sample stdev = sqrt(5/3), sem = stdev / 2.
    EXPECT_NEAR(st::sem(data), std::sqrt(5.0 / 3.0) / 2.0, 1e-12);

    auto single = nd::array(std::vector<double>{7.0});
    EXPECT_DOUBLE_EQ(st::sem(single), 0.0);     // n < 2 -> no spread to estimate
}

TEST(Stats, EmittedHelpers) {
    // Hist carries ndarray fields, so purrc emits no print glue for it — module_abi is the
    // emitted surface to touch here.
    EXPECT_STREQ(st::module_abi(), "stats");
}
