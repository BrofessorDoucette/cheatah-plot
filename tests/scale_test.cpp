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

// log_ticks — every arm: the non-positive and degenerate fallbacks, plain decades, strided decades,
// the 1-2-5 sub-decade subdivision (each mantissa's in/out combinations), and the no-candidate
// linear fallback.
TEST(Scale, LogTicks) {
    s::Range bad{-1.0, 10.0};              // lo <= 0 -> linear fallback
    auto fb = s::log_ticks(bad, 4);
    ASSERT_GT(nd::size_of(fb), 0);

    s::Range degenerate{5.0, 5.0};         // span <= 0 -> linear fallback (single tick)
    auto one = s::log_ticks(degenerate, 4);
    ASSERT_EQ(nd::size_of(one), 1);
    EXPECT_DOUBLE_EQ(one[0], 5.0);

    s::Range wide{1.0, 100.0};             // three decade marks, no stride
    auto dec = s::log_ticks(wide, 5);
    ASSERT_EQ(nd::size_of(dec), 3);
    EXPECT_DOUBLE_EQ(dec[0], 1.0);
    EXPECT_DOUBLE_EQ(dec[1], 10.0);
    EXPECT_DOUBLE_EQ(dec[2], 100.0);

    s::Range huge{1.0, 1e6};               // seven decades, target 4 -> stride 2
    auto strided = s::log_ticks(huge, 4);
    ASSERT_EQ(nd::size_of(strided), 4);
    EXPECT_DOUBLE_EQ(strided[0], 1.0);
    EXPECT_DOUBLE_EQ(strided[1], 100.0);
    EXPECT_DOUBLE_EQ(strided[3], 1e6);

    s::Range sub{1.0, 9.0};                // one decade mark only -> 1-2-5 subdivision
    auto mant = s::log_ticks(sub, 4);
    ASSERT_EQ(nd::size_of(mant), 3);
    EXPECT_DOUBLE_EQ(mant[0], 1.0);
    EXPECT_DOUBLE_EQ(mant[1], 2.0);
    EXPECT_DOUBLE_EQ(mant[2], 5.0);

    s::Range frac{0.15, 0.85};             // decade 0.1 below range: only 0.2 and 0.5 survive
    auto ff = s::log_ticks(frac, 4);
    ASSERT_EQ(nd::size_of(ff), 2);
    EXPECT_NEAR(ff[0], 0.2, 1e-12);
    EXPECT_NEAR(ff[1], 0.5, 1e-12);

    s::Range trapped{3.1, 4.7};            // no 1-2-5 mantissa inside -> linear fallback
    auto lin = s::log_ticks(trapped, 4);
    ASSERT_GT(nd::size_of(lin), 0);
    EXPECT_GE(lin[0], 3.1);
}

// to_pixel_log — the three non-positive fallbacks, the degenerate log-span guard, and the log map.
TEST(Scale, ToPixelLog) {
    s::Range r{1.0, 100.0};
    EXPECT_DOUBLE_EQ(s::to_pixel_log(1.0,   r, 0.0, 100.0),   0.0);
    EXPECT_DOUBLE_EQ(s::to_pixel_log(100.0, r, 0.0, 100.0), 100.0);
    EXPECT_DOUBLE_EQ(s::to_pixel_log(10.0,  r, 0.0, 100.0),  50.0);   // the log midpoint

    s::Range neg_lo{-1.0, 100.0};          // r.lo <= 0 -> linear fallback
    EXPECT_DOUBLE_EQ(s::to_pixel_log(50.0, neg_lo, 0.0, 100.0),
                     s::to_pixel(50.0, neg_lo, 0.0, 100.0));
    s::Range neg_hi{1.0, -100.0};          // r.hi <= 0 -> linear fallback
    EXPECT_DOUBLE_EQ(s::to_pixel_log(0.5, neg_hi, 0.0, 100.0),
                     s::to_pixel(0.5, neg_hi, 0.0, 100.0));
    EXPECT_DOUBLE_EQ(s::to_pixel_log(-2.0, r, 0.0, 100.0),           // value <= 0 -> fallback
                     s::to_pixel(-2.0, r, 0.0, 100.0));

    s::Range same{10.0, 10.0};             // log span == 0 -> px_lo
    EXPECT_DOUBLE_EQ(s::to_pixel_log(10.0, same, 7.0, 100.0), 7.0);
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
