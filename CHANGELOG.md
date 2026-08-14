# Changelog

All notable changes to cheatah-plot. This project is **alpha** — expect breaking changes
between releases. Public at github.com/BrofessorDoucette/cheatah-plot; a Biome Standard
member alongside the other cheatah extensions.

## v0.1.0-alpha (2026-08-14) — figures to pixels: the whole plotting stack, on CPU and GPU

The first release carries the whole arc from the `plot.scale` seed to rendered PNGs.

### The model layers (pure cheatah on ndarray)

- `plot.scale` grows log-axis geometry (decade `log_ticks` with 1-2-5 subdivision,
  `to_pixel_log`); `plot.color` (palettes, viridis/magma/coolwarm, a CSS-style name table);
  `plot.series` (the mark value + two-arity constructors for all nine mark kinds);
  `plot.stats` (histogram binning, the `linalg.lstsq` fit line, SEM via `statistics`);
  `plot.figure` (subplot grids, axes, the fluent free-function builder).
- Conventions that emerged from the language: two arities instead of default arguments,
  `list<Color>` palettes, fluent functions returning modified copies.

### The renderer (cheatah-plot's own layer on cheatah-gpu)

- Compute-shader rasterization: ONE Slang source (`plot_clear` + `plot_raster`), a 64-byte
  primitive ABI (segments, discs, rects, triangles, glyphs — the glyph's bitmap rides in the
  prim), CPU reduce → 16×16 tile binning → per-pixel integer src-over blending. Deterministic
  by construction.
- **Three lanes, one contract**: the C++ reference rasterizer is the executable spec; the
  emulated-Metal lane is BIT-EXACT against it (verified byte-for-byte); the Vulkan lane is
  tolerance-checked and runs on real hardware and llvmpipe. `render()` prefers a live GPU
  lane and falls back to the reference with a one-time notice; `CHEATAH_PLOT_FORCE_CPU=1`
  pins the reference path.
- Dependency-free stored-deflate PNG encoder (+ PPM) and an ORIGINAL 8×16 bitmap font.
- The purr surface: `plot.save(fig, path)` / `plot.render(fig)` after building any figure.

### Tested and documented to the house bar

- **100% line + function coverage** over the package (backend bring-up excluded per the
  cheatah-gpu-linalg precedent, held instead by the dedicated GPU-lane tests); 78 C++ tests.
- 17 systests through the real runtime — one per plotting FEATURE (every mark kind, styles,
  grouping, log axes, subplots, legends, fit overlays, PNG save with verified bytes).
- An `@par Example` block with a complete compilable program on EVERY public function,
  compile-verified by `scripts/check_doc_examples.sh` (a hard gate stage).
- 15 example programs render the gallery (`examples/purr_plot/run_examples.sh`); a Google
  Benchmark suite covers reduce, binning, raster fill-rate, and PNG encoding.
- The full QA ladder: coverage → docs → module sync → build → systests → biome-install →
  unit tests → ASan/UBSan → Valgrind → cppcheck → private-refs → doc examples.

### Docs

- DESIGN.md and the API outline rewritten to the real architecture: headless first, the
  renderer on cheatah-gpu's raw forwarders, windowing as a later `plot.window` layer; the
  manifest targets the v1.10.0-alpha toolchain and requires cheatah-gpu + cheatah-gpu-linalg.
