# cheatah-plot — design & architecture agreements

These are the load-bearing decisions cheatah-plot honors **at all times**. They are recorded here
(outside `plot/`, so the doc-coverage gate doesn't treat prose as API) — the contract the
implementation fills in, layer by layer.

## What cheatah-plot is

Dead-simple **cross-platform plotting**. `import plot`, hand it some numbers, get a beautiful
plot — as a PNG or as pixels you read back — with the GPU doing the rasterizing on Vulkan or
Metal, Linux/macOS today. A small, sharp renderer, not a graphics engine. Public and MIT;
installed as a cheatah extension via `biome add cheatah-plot`. Headless first: `plot.window`
(presentation) is a later layer, and nothing in the core depends on it.

## The layer contract: pure model → renderer on cheatah-gpu-linalg's context

- **Model layers** (`plot.scale`, `plot.color`, `plot.series`, `plot.stats`, `plot.figure`) are
  pure cheatah on `ndarray` — no device, fully unit-testable, 100% covered. A Figure is plain
  data; fluent free functions return modified copies (cheatah values are const).
- **The renderer dispatches on cheatah-gpu-linalg's device context** — the one small compute
  layer the cheatah stdlib extensions share (linear algebra, plotting). cheatah-plot owns no
  context, no buffer pool and no per-backend code: it owns its two kernels (`plot_clear` /
  `plot_raster`, handed to the context by directory-qualified name so they never collide with
  the linalg kernels the same context serves), the emulated-Metal stand-ins that make the
  software lane bit-exact with `raster_cpu`, and the upload → dispatch → download orchestration.
  Backend choice is the context's platform default (Metal on Apple, Vulkan elsewhere).
- **Reuse before writing** (the house rule): anything that is linear algebra goes through the
  stdlib `linalg` — and when arrays are device-resident, cheatah-gpu-linalg's `DeviceArray`
  overloads take over by ADL. `plot.stats` fits with `linalg.lstsq` and measures spreads with
  `statistics`; the renderer never re-implements math the stack already ships.

## The renderer (design; lands as `plot.renderer`)

Compute-shader rasterization — no graphics pipeline, no swapchain, deterministic output:

```
plot.figure → reduce (figure → layout → draw list, via plot.scale ticks)
            → CPU: primitive list in paint order → 16×16 tile binning
            → ONE Slang source (plot_clear + plot_raster), compiled per backend
            → storage-buffer RGBA8 framebuffer → save (PNG) or readback (ndarray pixels)
```

- Primitive set: segments, discs, squares, rects, triangles, glyphs, images — enough for every
  v1 mark. Per-pixel ordered src-over blending in INTEGER arithmetic (no atomics), so output is
  deterministic and the CPU reference path is bit-exact against the emulated-Metal lane.
- The C++ reference rasterizer (`raster_cpu`) doubles as the CPU fallback (machines with no
  GPU still save PNGs) and the Valgrind/memcheck target; Vulkan-lane goldens use a tight
  tolerance (float AA confined to coverage by the integer blending).
- PNG encoding is dependency-free (stored-deflate); the embedded font is a CC0 bitmap subset
  (provenance in NOTICE).

## Present vs readback

The renderer always draws into an offscreen framebuffer. v1 ships two sinks:

1. **Save** — `save(fig, path)` encodes the framebuffer to a file.
2. **Readback** — `render(fig, w, h)` returns host pixels (the stream frame).

"Present to a window" is a THIRD sink that `plot.window` adds later without touching the
renderer. Streaming a live plot to a website without JavaScript stays a first-class goal:
render offscreen → read back → encode → serve as an HTTP `multipart/x-mixed-replace` stream a
plain `<img>` tag renders live (roadmap; `plot.stream`).

## Written in cheatah (`.purr`) as much as possible

The model layers are authored in **cheatah** and compiled to the shipped headers with
`purrc --emit-library` (kept in sync by `scripts/gen-headers.sh`; the generated `plot/**/*.hpp`
+ `.sha512` sidecars are the biome artifact). C++ appears only where the renderer meets the
device (the kernels' CPU stand-ins, the dispatch orchestration) — the same split cheatah-gpu-linalg uses.

## Concurrency & memory ownership

cheatah-plot does **no threading of its own** and never deep-copies user data to draw it: an
`ndarray` handed to a mark constructor is shared into the Series (host side), and device
transfers happen once, at render time, under the renderer's control.

## Provisioning

`biome add cheatah-plot` → `scripts/install-deps.sh` provisions the userspace GPU stack (the
Vulkan loader/layers + Slang, per platform package manager); `scripts/doctor.sh` verifies it
(loader present, `slangc` compiles a shader). The model layers and the CPU render path need
none of it. Windows is a roadmap side quest (manual guidance for now).

## Platform support

- **Linux** (apt/dnf/pacman) and **macOS** (brew; native Metal via cheatah-gpu) are first-class.
  **Windows** is roadmap.
- **Backend is chosen at compile time** following cheatah-gpu's platform default; forcing a
  lane is a build flag, never runtime branching in plot code.

## Boundary: cheatah-plot is PUBLIC and generic

cheatah-plot (and cheatah-gpu) contain **zero** knowledge of any proprietary consumer.
cheatah-plot plots numbers; it knows nothing about trading, market data, models, or any
downstream domain. Downstream proprietary projects consume cheatah-plot as a dependency and
keep **all** of their usage on their side of the boundary — never referenced from, or leaked
into, the cheatah repos. `scripts/check_no_private_refs.sh` enforces this mechanically (tree,
commit messages, and the pre-push range scan).
