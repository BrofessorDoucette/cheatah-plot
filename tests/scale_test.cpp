// Unit tests for plot.scale — the pure axis geometry (data ranges, "nice" ticks, the data→pixel map).
// These exercise every branch of the generated plot/scale/scale.hpp so the QA gate's clang source-based
// coverage reports 100% lines + functions over the header. The .purr systest (systests/test_scale.purr)
// checks the same surface end-to-end through the cheatah runtime; this checks it in-process for coverage.
//
// The generated functions are templates (purrc emits `auto` / concept-constrained parameters), so each
// is instantiated with concrete argument types (double / int / Range / ndarray<double>) and every branch
// is driven within that one instantiation — that is what makes llvm-cov see the specialization covered.
#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "plot/scale/scale.hpp"

namespace s  = cheatah::scale;
namespace nd = cheatah::ndarray;

// data_range — empty -> [0,1]; the min/max scan (both the v<lo and v>hi update branches); and a flat
// series widened to a non-degenerate interval.
TEST(Scale, DataRange) {
    auto empty = nd::array(std::vector<double>{});
    auto re = s::data_range(empty);
    EXPECT_DOUBLE_EQ(re.lo, 0.0);
    EXPECT_DOUBLE_EQ(re.hi, 1.0);

    // 2.0 seeds lo/hi; -1.0 drives v<lo; 5.5 drives v>hi.
    auto vals = nd::array(std::vector<double>{2.0, -1.0, 5.5, 3.0});
    auto r = s::data_range(vals);
    EXPECT_DOUBLE_EQ(r.lo, -1.0);
    EXPECT_DOUBLE_EQ(r.hi, 5.5);

    // All-equal -> hi <= lo -> widened by 0.5 each side.
    auto flat = nd::array(std::vector<double>{4.0, 4.0, 4.0});
    auto rf = s::data_range(flat);
    EXPECT_LT(rf.lo, rf.hi);
    EXPECT_DOUBLE_EQ(rf.lo, 3.5);
    EXPECT_DOUBLE_EQ(rf.hi, 4.5);
}

// nice_step — each of the 1 / 2 / 5 / 10 residual branches (residual < 1.5, < 3.0, < 7.0, else).
TEST(Scale, NiceStep) {
    EXPECT_DOUBLE_EQ(s::nice_step(10.0, 10), 1.0);  // raw 1.0  -> residual 1   -> 1
    EXPECT_DOUBLE_EQ(s::nice_step(10.0, 5),  2.0);  // raw 2.0  -> residual 2   -> 2
    EXPECT_DOUBLE_EQ(s::nice_step(10.0, 2),  5.0);  // raw 5.0  -> residual 5   -> 5
    EXPECT_DOUBLE_EQ(s::nice_step(8.0,  1), 10.0);  // raw 8.0  -> residual 8   -> 10 (else)
    EXPECT_DOUBLE_EQ(s::nice_step(100.0, 5), 20.0); // spot-check a higher magnitude
}

// ticks — the normal count/fill path across a range, and the span<=0 single-tick guard.
TEST(Scale, Ticks) {
    s::Range r{0.0, 10.0};
    auto tk = s::ticks(r, 5);
    ASSERT_GT(nd::size_of(tk), 0);
    EXPECT_DOUBLE_EQ(tk[0], 0.0);          // first tick on a round step, >= lo

    s::Range degenerate{5.0, 5.0};         // span == 0 -> single tick at lo
    auto one = s::ticks(degenerate, 5);
    ASSERT_EQ(nd::size_of(one), 1);
    EXPECT_DOUBLE_EQ(one[0], 5.0);
}

// to_pixel — the linear map (endpoints + midpoint) and the span<=0 guard (returns px_lo).
TEST(Scale, ToPixel) {
    s::Range r{0.0, 10.0};
    EXPECT_DOUBLE_EQ(s::to_pixel(0.0,  r, 0.0, 100.0),   0.0);
    EXPECT_DOUBLE_EQ(s::to_pixel(10.0, r, 0.0, 100.0), 100.0);
    EXPECT_DOUBLE_EQ(s::to_pixel(5.0,  r, 0.0, 100.0),  50.0);

    s::Range degenerate{5.0, 5.0};         // span == 0 -> the value maps to px_lo
    EXPECT_DOUBLE_EQ(s::to_pixel(5.0, degenerate, 7.0, 100.0), 7.0);
}

// The emitted glue purrc generates for a value struct: operator<<, cheatah_pretty_print, module_abi.
// Touch them so coverage counts them (their doc contract lives in the .purr, not this header).
TEST(Scale, EmittedHelpers) {
    s::Range r{-1.0, 5.5};

    std::ostringstream os1;
    os1 << r;
    EXPECT_NE(os1.str().find("Range"), std::string::npos);

    std::ostringstream os2;
    r.cheatah_pretty_print(os2, 0);
    EXPECT_NE(os2.str().find("Range"), std::string::npos);

    EXPECT_STREQ(s::module_abi(), "scale");
}
