#pragma once

// cheatah-deps: linalg math ndarray statistics

/**
 * @file plot.hpp
 * @brief `import plot` — the easy, cross-platform plotting API.
 *
 * cheatah-plot draws a plot in a few lines. The model layers (scale/color/series/stats/figure)
 * are pure cheatah on `ndarray` — fully testable with no device — and the renderer is
 * cheatah-plot's OWN layer built directly on cheatah-gpu's raw Vulkan/Metal forwarders:
 * compute-shader rasterization into an offscreen framebuffer you save to a file or read back
 * as pixels (to stream a live plot — e.g. to a website, without JavaScript). Headless first;
 * windowing stays roadmap. See docs/DESIGN.md.
 *
 * This umbrella is the one hand-written C++ in the package — pure `#include`s of the generated submodule
 * headers, no logic — so `import plot` (and `import plot.<sub>`) resolve the whole `cheatah::plot::*`
 * surface. Every submodule is authored in `.purr` under `plot/<sub>/<sub>.purr` and transpiled to its
 * committed header by `scripts/gen-headers.sh`.
 *
 * Submodules:
 *   - plot.scale    — the pure axis geometry: ranges, "nice" linear + log ticks, data→pixel.  [working]
 *   - plot.color    — colours, categorical palettes, viridis/magma/coolwarm colormaps.        [working]
 *   - plot.series   — the mark data + style value every plot call builds.                     [working]
 *   - plot.stats    — histogram binning, the lstsq fit line, error magnitudes.                [working]
 *   - plot.figure   — the figure model: subplots, axes, the fluent building API.              [working]
 *   - plot.renderer — the 2D renderer (offscreen render target -> file or readback).          [roadmap]
 *   - plot.window   — windowing + presentation.                                               [roadmap]
 */

#include "color/color.hpp"
#include "scale/scale.hpp"
#include "series/series.hpp"
#include "stats/stats.hpp"
#include "figure/figure.hpp"
#include "renderer/render.hpp"

namespace cheatah::plot {
// The renderer's front door, surfaced at the package root so purr code writes
// `plot.save(fig, "out.png")` / `plot.render(fig)` after building a figure.
using renderer::render;
using renderer::save;
}  // namespace cheatah::plot
