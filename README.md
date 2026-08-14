# cheatah-plot

Dead-simple **cross-platform plotting** for [cheatah](https://github.com/BrofessorDoucette/cheatah).
Hand it some numbers, get a beautiful plot — as a PNG, or as pixels you read back and stream anywhere.

```cheatah
import plot

let xs = # ... an ndarray of x values
let ys = # ... an ndarray of y values
let fig = plot.line(plot.new_figure(), xs, ys)
fig = plot.title(fig, "my line")
plot.save(fig, "line.png")     # a real PNG, rendered by cheatah-plot
```

The model layers (`plot.scale` / `plot.color` / `plot.series` / `plot.stats` / `plot.figure`) are
pure cheatah on `ndarray` — fully testable with no device. The renderer is cheatah-plot's OWN
layer built directly on [cheatah-gpu](https://github.com/BrofessorDoucette/cheatah-gpu)'s raw
Vulkan/Metal forwarders: compute-shader rasterization into an offscreen framebuffer you save to a
file or read back (to stream a live plot to a website, no JavaScript). Anything that is linear
algebra rides the stdlib `linalg` — and, for device-resident arrays,
[cheatah-gpu-linalg](https://github.com/BrofessorDoucette/cheatah-gpu-linalg)'s overloads.
Headless first; windowing is roadmap. See [docs/DESIGN.md](docs/DESIGN.md) for the architecture.

## Install

```sh
biome add cheatah-plot        # fetches cheatah-plot + its cheatah-gpu stack
```

Then `import plot` and draw. `scripts/doctor.sh` verifies your machine is ready for the GPU path
(Vulkan loader, `slangc`); the model layers and the CPU render path need no GPU at all.

## Modules

| `import …` | what | status |
|------------|------|--------|
| **`plot`** | the umbrella — the whole `plot.*` surface in one import | working |
| **`plot.scale`** | the pure axis geometry: ranges, "nice" linear + log ticks, data→pixel | working |
| **`plot.color`** | colours, categorical palettes, viridis/magma/coolwarm colormaps | working |
| **`plot.series`** | the mark data + style value every plot call builds | working |
| **`plot.stats`** | histogram binning, the `linalg.lstsq` fit line, error magnitudes | working |
| **`plot.figure`** | the figure model: subplots, axes, the fluent building API | working |
| **`plot.renderer`** | the 2D renderer: CPU reference + bit-exact emulated-Metal + Vulkan lanes | working |
| **`plot.window`** | windowing + presentation | roadmap |

## Status

The whole stack works end to end: build figures from cheatah, render them to PNGs — on the
CPU reference rasterizer everywhere, and through the GPU lanes (Vulkan, emulated Metal) when
a device is present, with `render()` falling back cleanly. Fifteen example programs draw the
gallery (`examples/purr_plot/`). `plot.window` (presentation) is the next layer.

<!-- coverage:start -->
| Metric | plot package |
|--------|--------------|
| **Lines** | 100.00% (394/394) |
| **Functions** | 100.00% (121/121) |
| Regions | 97.51% |
| Branches | 89.74% |
<!-- coverage:end -->

## License

MIT — © 2026 BigBrain LLC (Joshua Doucette, on its behalf). See [LICENSE](LICENSE) and [NOTICE](NOTICE).
