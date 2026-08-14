#pragma once

/**
 * @file reduce.hpp
 * @brief Figure → layout → DrawList: the reduction from the pure model to pixel-space
 *        primitives, REUSING the generated model layers (`plot.scale` ticks + maps,
 *        `plot.color` palettes/colormaps) instead of re-deriving any of it.
 *
 * Layout: each subplot gets its grid cell; fixed margins carve the axes viewport (left 56 for
 * y tick labels, bottom 36 for x labels, top 24 — 44 with a title — right 12). The y pixel
 * axis is FLIPPED at the `to_pixel` call sites (screen y grows downward). Every series'
 * geometry is clipped to the viewport where it can bleed (bars/rects clamp; lines rely on the
 * viewport border being drawn over stray AA). Auto axis limits come from `scale.data_range`
 * over every series' arrays; auto colours cycle the figure's palette per subplot.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../plot.hpp"
#include "drawlist.hpp"

namespace cheatah::plot::renderer {

namespace detail {

/// The house canvas colours: white ground, near-black ink, light grid.
inline constexpr std::uint32_t kWhite = 0xFFFFFFFFu;
inline constexpr std::uint32_t kInk = 0xFF202020u;
inline constexpr std::uint32_t kGrid = 0xFFE0E0E0u;

/// Format a tick value the way an axis label reads best: %.6g, with float-noise around zero
/// snapped so no axis ever reads "-0". @complexity O(1). @alloc the returned string.
inline std::string fmt_tick(double v) {
    if (std::abs(v) < 1e-12) v = 0.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return std::string(buf);
}

/// A resolved axis: finite lo/hi (auto filled from data), plus its log flag.
struct AxisSpan {
    double lo = 0.0;
    double hi = 1.0;
    bool log = false;
};

/// Pack a model Color for the kernel ABI, resolving the auto sentinel from the palette at
/// @p index. @complexity O(1). @alloc none.
inline std::uint32_t resolve_rgba(const cheatah::color::Color& c,
                                  const std::vector<cheatah::color::Color>& palette,
                                  std::size_t index) {
    if (c.a == 0.0 && !palette.empty()) {
        const auto& pc = palette[index % palette.size()];
        return pack_rgba(pc.r, pc.g, pc.b, pc.a);
    }
    return pack_rgba(c.r, c.g, c.b, c.a);
}

/// Fold one ndarray's finite values into a running [lo, hi]. Reads the raw contiguous buffer
/// so 2-D grids (heatmap z) fold the same as vectors — every array the model layers build is
/// freshly constructed and contiguous. @complexity O(n). @alloc none.
inline void fold_range(cheatah::ndarray::basic_ndarray<double>& a, double& lo, double& hi,
                       bool& any) {
    const long long n = cheatah::ndarray::size_of(a);
    if (n == 0) return;
    const double* d = a.buffer()->data();
    for (long long i = 0; i < n; ++i) {
        const double v = d[i];
        if (!std::isfinite(v)) continue;
        if (!any) { lo = hi = v; any = true; continue; }
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
}

/// Resolve one axis: fixed limits pass through; NaN limits come from the folded data range,
/// widened when degenerate exactly like `scale.data_range`; a log axis floors lo to a positive
/// epsilon. @complexity O(total data). @alloc none.
inline AxisSpan resolve_axis(const cheatah::figure::Axis& ax, double data_lo, double data_hi,
                             bool any_data) {
    AxisSpan s;
    s.log = (ax.scale.kind == "log");
    double lo = ax.lo, hi = ax.hi;
    if (std::isnan(lo) || std::isnan(hi)) {
        double alo = any_data ? data_lo : 0.0;
        double ahi = any_data ? data_hi : 1.0;
        if (ahi <= alo) { alo -= 0.5; ahi += 0.5; }
        const double pad = (ahi - alo) * 0.05;              // 5% breathing room
        if (std::isnan(lo)) lo = alo - pad;
        if (std::isnan(hi)) hi = ahi + pad;
    }
    if (s.log) {
        if (hi <= 0.0) hi = 1.0;
        if (lo <= 0.0) lo = hi / 1000.0;                     // decade floor for stray zeros
    }
    s.lo = lo;
    s.hi = hi;
    return s;
}

/// Map a data value onto a pixel span through the resolved axis (log-aware), reusing the
/// generated `scale` maps. @complexity O(1). @alloc none.
inline float map_px(double v, const AxisSpan& s, double px_lo, double px_hi) {
    cheatah::scale::Range r{s.lo, s.hi};
    const double p = s.log ? cheatah::scale::to_pixel_log(v, r, px_lo, px_hi)
                           : cheatah::scale::to_pixel(v, r, px_lo, px_hi);
    return static_cast<float>(p);
}

/// The pixel viewport of one subplot cell (the axes box).
struct Viewport {
    float x0, y0, x1, y1;   ///< left, top, right, bottom in framebuffer pixels.
};

/// Emit a scatter marker of the series' shape at (px, py). @complexity O(1). @alloc amortized.
inline void emit_marker(DrawList& dl, const std::string& marker, float px, float py, float size,
                        std::uint32_t rgba) {
    const float h = size * 0.5f;
    if (marker == "square") {
        push_rect(dl, px - h, py - h, px + h, py + h, rgba);
    } else if (marker == "diamond") {
        push_tri(dl, px, py - h, px + h, py, px, py + h, rgba);
        push_tri(dl, px, py - h, px - h, py, px, py + h, rgba);
    } else {
        push_disc(dl, px, py, h, rgba);
    }
}

}  // namespace detail

/**
 * Reduce a Figure to the primitive list a rasterizer draws — layout, axes, ticks + labels,
 * grid, every mark kind, and per-subplot legends, in paint order.
 *
 * @param fig The figure model (subplots, axes, series, palette).
 * @param width The framebuffer width in pixels.
 * @param height The framebuffer height in pixels.
 * @return The draw list, ready for @ref bin_prims + @ref raster_cpu (or the GPU kernels).
 * @complexity O(total data points + ticks + glyphs).
 * @alloc the returned list (plus transient tick arrays).
 * @test plot:reduce
 */
inline DrawList reduce(cheatah::figure::Figure& fig, std::uint32_t width, std::uint32_t height) {
    DrawList dl;
    push_rect(dl, 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
              detail::kWhite);   // the page

    const float cell_w = static_cast<float>(width) / static_cast<float>(fig.cols);
    const float cell_h = static_cast<float>(height) / static_cast<float>(fig.rows);

    for (std::size_t si = 0; si < fig.subplots.size(); ++si) {
        auto& sp = fig.subplots[si];
        const float cx0 = static_cast<float>(si % static_cast<std::size_t>(fig.cols)) * cell_w;
        const float cy0 = static_cast<float>(si / static_cast<std::size_t>(fig.cols)) * cell_h;
        const bool titled = !sp.title.empty();
        detail::Viewport vp{cx0 + 56.0f, cy0 + (titled ? 44.0f : 24.0f), cx0 + cell_w - 12.0f,
                            cy0 + cell_h - 36.0f};
        if (vp.x1 - vp.x0 < 16.0f || vp.y1 - vp.y0 < 16.0f) continue;   // cell too small to draw

        // -- auto limits from every series' data ------------------------------------------------
        double xlo = 0, xhi = 0, ylo = 0, yhi = 0;
        bool anyx = false, anyy = false;
        for (auto& s : sp.series) {
            detail::fold_range(s.x, xlo, xhi, anyx);
            detail::fold_range(s.y, ylo, yhi, anyy);
            detail::fold_range(s.ylo, ylo, yhi, anyy);
            detail::fold_range(s.yhi, ylo, yhi, anyy);
            if (s.kind == "heatmap" && cheatah::ndarray::size_of(s.z) > 0) {
                const auto& shape = s.z.shape();
                if (shape.size() == 2) {
                    if (!anyx) { xlo = 0; xhi = static_cast<double>(shape[1]); anyx = true; }
                    if (!anyy) { ylo = 0; yhi = static_cast<double>(shape[0]); anyy = true; }
                }
            }
        }
        const detail::AxisSpan xs = detail::resolve_axis(sp.x, xlo, xhi, anyx);
        const detail::AxisSpan ys = detail::resolve_axis(sp.y, ylo, yhi, anyy);
        // Screen y grows downward: the y map's pixel span is (bottom, top).
        auto X = [&](double v) { return detail::map_px(v, xs, vp.x0, vp.x1); };
        auto Y = [&](double v) { return detail::map_px(v, ys, vp.y1, vp.y0); };

        // -- grid + ticks + labels (via the generated scale locators) ---------------------------
        cheatah::scale::Range xr{xs.lo, xs.hi}, yr{ys.lo, ys.hi};
        auto xticks = xs.log ? cheatah::scale::log_ticks(xr, sp.x.ticks)
                             : cheatah::scale::ticks(xr, sp.x.ticks);
        auto yticks = ys.log ? cheatah::scale::log_ticks(yr, sp.y.ticks)
                             : cheatah::scale::ticks(yr, sp.y.ticks);
        for (long long i = 0; i < cheatah::ndarray::size_of(xticks); ++i) {
            const float px = X(xticks[i]);
            push_seg(dl, px, vp.y0, px, vp.y1, 0.5f, detail::kGrid);
            const std::string lbl = detail::fmt_tick(xticks[i]);
            push_text(dl, px - static_cast<float>(text_width(lbl)) * 0.5f, vp.y1 + 6.0f, lbl,
                      detail::kInk);
        }
        for (long long i = 0; i < cheatah::ndarray::size_of(yticks); ++i) {
            const float py = Y(yticks[i]);
            push_seg(dl, vp.x0, py, vp.x1, py, 0.5f, detail::kGrid);
            const std::string lbl = detail::fmt_tick(yticks[i]);
            push_text(dl, vp.x0 - 6.0f - static_cast<float>(text_width(lbl)),
                      py - static_cast<float>(kGlyphHeight) * 0.5f, lbl, detail::kInk);
        }
        push_rect(dl, vp.x0, vp.y0, vp.x1, vp.y1, detail::kInk, 0.5f);   // the axes border

        // -- titles + axis labels ---------------------------------------------------------------
        if (titled)
            push_text(dl, (vp.x0 + vp.x1) * 0.5f - static_cast<float>(text_width(sp.title)) * 0.5f,
                      cy0 + 12.0f, sp.title, detail::kInk);
        if (!sp.x.label.empty())
            push_text(dl,
                      (vp.x0 + vp.x1) * 0.5f - static_cast<float>(text_width(sp.x.label)) * 0.5f,
                      vp.y1 + 20.0f, sp.x.label, detail::kInk);
        if (!sp.y.label.empty())
            push_text(dl, cx0 + 4.0f, vp.y0 - 18.0f, sp.y.label, detail::kInk);

        // -- marks, in series order -------------------------------------------------------------
        for (std::size_t k = 0; k < sp.series.size(); ++k) {
            auto& s = sp.series[k];
            const std::uint32_t rgba = detail::resolve_rgba(s.color, fig.palette, k);
            const long long n = cheatah::ndarray::size_of(s.x);
            const float hw = static_cast<float>(s.width) * 0.5f;

            if (s.kind == "line" || s.kind == "step") {
                float dash_on = 0.0f, dash_off = 0.0f;
                if (cheatah::ndarray::size_of(s.dash) >= 2) {
                    dash_on = static_cast<float>(s.dash[0]);
                    dash_off = static_cast<float>(s.dash[1]);
                }
                for (long long i = 0; i + 1 < n; ++i) {
                    const double y0v = s.y[i], y1v = s.y[i + 1];
                    if (!std::isfinite(y0v) || !std::isfinite(y1v)) continue;   // NaN = gap
                    const float x0p = X(s.x[i]), x1p = X(s.x[i + 1]);
                    const float y0p = Y(y0v), y1p = Y(y1v);
                    if (s.kind == "step") {
                        push_seg(dl, x0p, y0p, x1p, y0p, hw, rgba, dash_on, dash_off);
                        push_seg(dl, x1p, y0p, x1p, y1p, hw, rgba, dash_on, dash_off);
                    } else {
                        push_seg(dl, x0p, y0p, x1p, y1p, hw, rgba, dash_on, dash_off);
                    }
                }
            } else if (s.kind == "scatter") {
                for (long long i = 0; i < n; ++i) {
                    if (!std::isfinite(s.y[i])) continue;
                    std::uint32_t c = rgba;
                    if (!s.by.empty() && static_cast<std::size_t>(i) < s.by.size() &&
                        !fig.palette.empty()) {
                        const auto& pc = fig.palette[static_cast<std::size_t>(s.by[i]) %
                                                     fig.palette.size()];
                        c = pack_rgba(pc.r, pc.g, pc.b, pc.a);
                    }
                    detail::emit_marker(dl, s.marker, X(s.x[i]), Y(s.y[i]),
                                        static_cast<float>(s.size), c);
                }
            } else if (s.kind == "bar") {
                double slot = 1.0;
                if (n > 1) {
                    slot = std::abs(s.x[1] - s.x[0]);
                    for (long long i = 1; i + 1 < n; ++i)
                        slot = std::min(slot, std::abs(s.x[i + 1] - s.x[i]));
                }
                const float base = Y(std::clamp(0.0, ys.lo, ys.hi));
                for (long long i = 0; i < n; ++i) {
                    if (!std::isfinite(s.y[i])) continue;
                    const float half = static_cast<float>(
                        std::abs(X(s.x[i] + slot * s.width * 0.5) - X(s.x[i])));
                    const float px = X(s.x[i]);
                    const float py = Y(s.y[i]);
                    const float top = std::min(py, base), bot = std::max(py, base);
                    push_rect(dl, px - half, top, px + half, bot, rgba,
                              s.fill ? 0.0f : 0.75f);
                }
            } else if (s.kind == "area") {
                const float base = Y(std::clamp(0.0, ys.lo, ys.hi));
                for (long long i = 0; i + 1 < n; ++i) {
                    if (!std::isfinite(s.y[i]) || !std::isfinite(s.y[i + 1])) continue;
                    const float x0p = X(s.x[i]), x1p = X(s.x[i + 1]);
                    const float y0p = Y(s.y[i]), y1p = Y(s.y[i + 1]);
                    push_tri(dl, x0p, y0p, x1p, y1p, x0p, base, rgba);
                    push_tri(dl, x1p, y1p, x1p, base, x0p, base, rgba);
                }
            } else if (s.kind == "fill_between") {
                for (long long i = 0; i + 1 < static_cast<long long>(
                                                  cheatah::ndarray::size_of(s.ylo)); ++i) {
                    const float x0p = X(s.x[i]), x1p = X(s.x[i + 1]);
                    const float l0 = Y(s.ylo[i]), l1 = Y(s.ylo[i + 1]);
                    const float h0 = Y(s.yhi[i]), h1 = Y(s.yhi[i + 1]);
                    push_tri(dl, x0p, h0, x1p, h1, x0p, l0, rgba);
                    push_tri(dl, x1p, h1, x1p, l1, x0p, l0, rgba);
                }
            } else if (s.kind == "errorbar") {
                for (long long i = 0; i < n; ++i) {
                    const float px = X(s.x[i]);
                    const float lo_p = Y(s.ylo[i]), hi_p = Y(s.yhi[i]);
                    push_seg(dl, px, lo_p, px, hi_p, hw, rgba);
                    push_seg(dl, px - 4.0f, lo_p, px + 4.0f, lo_p, hw, rgba);
                    push_seg(dl, px - 4.0f, hi_p, px + 4.0f, hi_p, hw, rgba);
                    push_disc(dl, px, Y(s.y[i]), 2.5f, rgba);
                }
            } else if (s.kind == "stem") {
                const float base = Y(std::clamp(0.0, ys.lo, ys.hi));
                for (long long i = 0; i < n; ++i) {
                    const float px = X(s.x[i]);
                    push_seg(dl, px, base, px, Y(s.y[i]), hw, rgba);
                    push_disc(dl, px, Y(s.y[i]), static_cast<float>(s.size) * 0.5f, rgba);
                }
            } else if (s.kind == "heatmap") {
                const auto& shape = s.z.shape();
                if (shape.size() != 2) continue;
                const long long rows = static_cast<long long>(shape[0]);
                const long long cols = static_cast<long long>(shape[1]);
                double zlo = 0, zhi = 0;
                bool anyz = false;
                detail::fold_range(s.z, zlo, zhi, anyz);
                const double span = (zhi > zlo) ? (zhi - zlo) : 1.0;
                const double* zd = s.z.buffer()->data();   // contiguous row-major grid
                for (long long r = 0; r < rows; ++r)
                    for (long long ccol = 0; ccol < cols; ++ccol) {
                        const double z = zd[r * cols + ccol];
                        const auto cc = cheatah::color::viridis((z - zlo) / span);
                        push_rect(dl, X(static_cast<double>(ccol)),
                                  Y(static_cast<double>(r + 1)),
                                  X(static_cast<double>(ccol + 1)), Y(static_cast<double>(r)),
                                  pack_rgba(cc.r, cc.g, cc.b, cc.a));
                    }
            }
        }

        // -- legend: labeled series, top-right --------------------------------------------------
        if (sp.legend) {
            std::vector<std::pair<std::string, std::uint32_t>> entries;
            for (std::size_t k = 0; k < sp.series.size(); ++k)
                if (!sp.series[k].label.empty())
                    entries.emplace_back(sp.series[k].label,
                                         detail::resolve_rgba(sp.series[k].color, fig.palette, k));
            if (!entries.empty()) {
                int wmax = 0;
                for (auto& e : entries) wmax = std::max(wmax, text_width(e.first));
                const float lw = 26.0f + static_cast<float>(wmax) + 8.0f;
                const float lh = static_cast<float>(entries.size()) * 20.0f + 8.0f;
                const float lx = vp.x1 - lw - 8.0f, ly = vp.y0 + 8.0f;
                push_rect(dl, lx, ly, lx + lw, ly + lh, detail::kWhite);
                push_rect(dl, lx, ly, lx + lw, ly + lh, detail::kInk, 0.5f);
                for (std::size_t e = 0; e < entries.size(); ++e) {
                    const float ey = ly + 8.0f + static_cast<float>(e) * 20.0f;
                    push_rect(dl, lx + 6.0f, ey + 2.0f, lx + 20.0f, ey + 12.0f,
                              entries[e].second);
                    push_text(dl, lx + 26.0f, ey, entries[e].first, detail::kInk);
                }
            }
        }
    }
    return dl;
}

}  // namespace cheatah::plot::renderer
