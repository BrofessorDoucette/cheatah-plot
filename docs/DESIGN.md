# cheatah-plot — design & architecture agreements

These are the load-bearing decisions cheatah-plot honors **at all times**. They are recorded here
(outside `plot/`, so the doc-coverage gate doesn't treat prose as API) because the code under `plot/`
is currently an **outline** — these notes are the contract the implementation fills in.

## What cheatah-plot is

Dead-simple **cross-platform plotting** on the GPU. `import plot`, hand it some numbers, get a window
with a beautiful plot — on Vulkan or Metal, Linux/macOS today. A small, sharp renderer, not a graphics
engine. Public and MIT; installed as a cheatah extension via `biome add cheatah-plot`.

## It renders through cheatah-gpu's EASY layer only

cheatah-gpu exposes three interfaces (see its `docs/DESIGN.md`): `gpu.vulkan` (1:1 with native Vulkan),
`gpu.metal` (1:1 with native Metal), and **`gpu`** — the slim, statically-typed, cross-platform layer
that encapsulates the native detail and does the intelligent optimizations. **cheatah-plot uses ONLY
the `gpu` easy layer.** It never touches `gpu.vulkan`/`gpu.metal` directly, so a single plot is portable
across backends with no `#if` in cheatah-plot.

- The easy `gpu` layer is **grown as cheatah-plot needs it** — cheatah-plot is the first real consumer
  and drives what the easy layer must offer (context, buffers/images, swapchain present, offscreen
  render targets + readback, a 2D pipeline).
- The easy layer is **built ON the 1:1 layers**: it composes `gpu.vulkan`/`gpu.metal` forwarders and
  **never reimplements a native call**, so every native call has one implementation (covered by the 1:1
  device-matrix tests) and the easy layer adds only orchestration + policy.

## Written in cheatah (`.purr`) as much as possible

The library and its renderer are authored in **cheatah** — like cheatah's own stdlib `requests`/`parsers`
libraries — and compiled to the shipped headers with `purrc --emit-library` (kept in sync by
`scripts/gen-headers.sh`; the generated `plot/**/*.hpp` + `.sha512` sidecars are the biome artifact).
The **only** C++ is a thin shim where C interop is unavoidable — the windowing binding (GLFW). All plot
logic (tessellation, axes/ticks, layout, the figure API) is statically-typed cheatah on the easy `gpu`
layer.

## The render pipeline — decoupled so a frame can go to a WINDOW or a STREAM

```
plot.figure  →  plot.renderer  →  gpu OFFSCREEN render target (a readable color image)  →  one of:
 (the API)      (tessellate:                                                                 ├─ PRESENT → window swapchain (desktop)
                 line/axes/text →                                                            └─ READBACK → host image → encode → stream
                 vertex buffers, draw)
```

Core principle: **the renderer always draws into an offscreen framebuffer.** "Presenting" is a *separate*
step with two sinks:

1. **Present** — blit/copy the offscreen image onto a window swapchain image and present it (the desktop
   path: `figure().show()`).
2. **Readback** — copy the offscreen image to a host buffer (`figure().frame()` / `save_png()`), for
   encoding + streaming.

This keeps desktop and headless identical up to the last step, and makes the streaming use case a first-
class citizen rather than a bolt-on.

### Streaming a live plot to a website — WITHOUT JavaScript (roadmap; `plot.stream`)

To be designed when we get there, but the architecture is built for it: render offscreen → read back →
encode each frame (e.g. PNG/JPEG) → serve over cheatah's net stack as an HTTP
`multipart/x-mixed-replace` **MJPEG** stream. A plain HTML `<img src="…">` renders that live, updating in
place, with **zero JavaScript**. No canvas, no WASM, no client code.

## Concurrency & memory ownership (inherited from cheatah-gpu)

cheatah-plot does **no threading of its own** and follows cheatah-gpu's **no-copy array borrow** model:
a cheatah `ndarray` handed to `line(x, y)` is leased to the GPU (a non-owning view; the caller keeps
ownership) under the CPU↔GPU interface guard, released when the draw completes. We never deep-copy the
user's data to draw it.

## Windowing

**GLFW** is the default windowing backend (create window, poll input, framebuffer size, close, and a
Vulkan/Metal **surface** for present), behind a thin `plot.window` interface + a small C++ shim.
**SDL3** may be evaluated as an alternative **only if** it can be pulled in lean (it decreases build
times and is the most broadly supported); the interface keeps the renderer independent of the choice.

## Provisioning

`biome add cheatah-plot` → `scripts/install-deps.sh` provisions the userspace stack (GLFW + the Vulkan
loader/layers + Slang, per platform package manager); `scripts/doctor.sh` verifies it (loader, `slangc`
compiles a shader, GLFW present). Windows is a roadmap side quest (manual guidance for now).

## Platform support

- **Linux** (apt/dnf/pacman) and **macOS** (brew; native Metal preferred via cheatah-gpu, MoltenVK
  fallback) are first-class. **Windows** is roadmap.
- **Backend is chosen at compile time** by cheatah-gpu (`gpu/backend.hpp`); cheatah-plot carries no
  per-backend code.

## Boundary: cheatah-plot is PUBLIC and generic

cheatah-plot (and cheatah-gpu) contain **zero** knowledge of any proprietary consumer. cheatah-plot
plots numbers; it knows nothing about trading, market data, models, or any downstream domain. Downstream
proprietary projects consume cheatah-plot as a dependency and keep **all** of their usage on their side
of the boundary — never referenced from, or leaked into, the cheatah repos.
