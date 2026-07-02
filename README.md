# cheatah-plot

Dead-simple **cross-platform plotting** on the GPU, for [cheatah](https://github.com/BrofessorDoucette/cheatah).
Hand it some numbers, get a beautiful plot in a window — on Vulkan or Metal, without learning either.

```cheatah
import plot

let xs = # ... an ndarray of x values
let ys = # ... an ndarray of y values
plot.figure().line(xs, ys).show()      # a window with your line plot
```

cheatah-plot renders through [cheatah-gpu](https://github.com/BrofessorDoucette/cheatah-gpu)'s easy,
cross-platform `gpu` layer and presents to a GLFW window — or to an offscreen framebuffer you can read
back (to stream a live plot to a website, no JavaScript). It's a small, sharp renderer, authored in
cheatah, not a graphics engine. See [docs/DESIGN.md](docs/DESIGN.md) for the architecture.

## Install

```sh
biome add cheatah-plot        # fetches cheatah-plot + cheatah-gpu and provisions the GPU + GLFW stack
```

Then `import plot` and draw. `scripts/doctor.sh` verifies your machine is ready (Vulkan loader, `slangc`,
GLFW).

## Modules

| `import …` | what | status |
|------------|------|--------|
| **`plot`** | the easy figure API — `figure().line(x, y).show()` | roadmap |
| **`plot.scale`** | the pure plotting geometry: data ranges, "nice" axis ticks, data→pixel map | working |
| **`plot.figure`** | figures + series (line first) + axes/labels | roadmap |
| **`plot.renderer`** | the 2D renderer (offscreen render target → present or readback) | roadmap |
| **`plot.window`** | windowing + surface (GLFW backend) | roadmap |

## Status

Early. The pure plotting geometry (`plot.scale`) is the working seed; the GPU renderer, windowing, and
the figure API are built out on top as cheatah-gpu's easy `gpu` layer grows (see the roadmap in
[docs/DESIGN.md](docs/DESIGN.md)).

<!-- coverage:start -->
| Metric | plot package |
|--------|--------------|
| **Lines** | 100.00% (25/25) |
| **Functions** | 100.00% (7/7) |
| Regions | 100.00% |
| Branches | 100.00% |
<!-- coverage:end -->

## License

MIT — © 2026 BigBrain LLC (Joshua Doucette, on its behalf). See [LICENSE](LICENSE) and [NOTICE](NOTICE).
