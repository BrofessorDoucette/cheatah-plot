#pragma once

// cheatah-deps: math ndarray

/**
 * @file plot.hpp
 * @brief `import plot` — the easy, cross-platform plotting API.
 *
 * cheatah-plot draws a plot in a few lines, on whatever GPU you have. It renders through cheatah-gpu's
 * easy, cross-platform `gpu` layer (which composes the 1:1 `gpu.vulkan` / `gpu.metal` surfaces), presents
 * to a GLFW window OR to an offscreen framebuffer you can read back (to stream a live plot — e.g. to a
 * website, without JavaScript), and is authored in cheatah (`.purr`) as much as possible. See
 * docs/DESIGN.md.
 *
 * This umbrella is the one hand-written C++ in the package — pure `#include`s of the generated submodule
 * headers, no logic — so `import plot` (and `import plot.<sub>`) resolve the whole `cheatah::plot::*`
 * surface. Every submodule is authored in `.purr` under `plot/<sub>/<sub>.purr` and transpiled to its
 * committed header by `scripts/gen-headers.sh`.
 *
 * Submodules:
 *   - plot.scale    — the pure axis geometry: data ranges, "nice" ticks, the data→pixel map.  [working]
 *   - plot.figure   — the ergonomic figure API (figure().line(x, y).show()).                  [roadmap]
 *   - plot.renderer — the 2D renderer (offscreen render target -> present or readback).        [roadmap]
 *   - plot.window   — windowing + surface (GLFW backend).                                      [roadmap]
 */

#include "scale/scale.hpp"

namespace cheatah::plot {}
