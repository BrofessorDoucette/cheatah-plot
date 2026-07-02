# cheatah-plot — the `import plot` API (design outline)

This is the **user-facing API outline**: how people will actually plot with cheatah-plot. It is written
first, deliberately, because it dictates what the renderer needs — and therefore what the third,
**easy `gpu`** API in cheatah-gpu must provide (see the last section). Nothing here is GPU-specific;
the pure model + ergonomics are what we lock down now.

Status: proposal for review. `plot.scale` is the only piece implemented so far.

## Two audiences, one library

We have two kinds of users and we refuse to alienate either:

- **The "just give me a plot" / matplotlib-pyplot crowd** — they want `plot.figure().line(x, y).show()`
  and would *never* adopt cheatah if it meant learning a whole new mental model.
- **The "I like declaring a spec" / plotly crowd** — they like handing the library a structured
  description of the figure and getting it back rendered.

cheatah-plot serves both with **one object model** exposed through **two front-ends that are views of the
same values** — not two competing, incompatible systems (which is mistake #1 of *both* prior libraries).

## What we are learning from (the mistakes to avoid)

Both matplotlib and plotly ship **two competing APIs each**, and both are internally inconsistent. The
worked example everyone hits — error bars — shows it plainly:

```python
# plotly graph_objects: a stringly-typed dict matrix
error_y=dict(type='data',    array=[...],  arrayminus=[...], symmetric=False, visible=True)
error_y=dict(type='percent', value=15,     valueminus=25,    symmetric=False)
error_y=dict(type='constant',value=0.1,    color='purple',   thickness=1.5,   width=3)
# plotly express: totally different — column-name strings, needs a DataFrame
px.scatter(df, x="sw", y="sl", error_y="e_plus", error_y_minus="e_minus")
```

```python
# matplotlib: a stateful global API AND an OO API that do the same thing two ways
plt.plot(x, y, 'ro'); plt.xlabel('t'); plt.title('h')      # magic 'ro' string; hidden current-axes
fig, ax = plt.subplots(); ax.plot(x, y, color='r', marker='o'); ax.set_xlabel('t')
```

| Prior-library mistake | cheatah-plot rule |
|---|---|
| **Two rival APIs** (pyplot vs OO; express vs graph_objects) that don't interoperate | **One model, two views.** The fluent and declarative forms build the *same* `Figure`; you can mix them. |
| **Global mutable state** (`plt.gca()`/`gcf()`, "current figure") — spooky action at a distance | **No globals.** A `Figure` is always an explicit value you hold and pass. |
| **Stringly-typed magic** (`'ro'`, `type='data'`/`'percent'`/`'constant'`) | **Typed options + plain arithmetic.** No format-string mini-language, no `type=` enum-in-a-string. |
| **Aliases for everything** (`color`/`c`, `linewidth`/`lw`, `linestyle`/`ls`) | **One name per concept.** `color`, `width`, `dash`. No aliases, ever. |
| **Boilerplate** (`visible=True`, `dict(...)` nesting) | **Sane defaults.** A series you add is visible. Options are default-valued keyword args, not nested dicts. |
| **`array`/`arrayminus`, `value`/`valueminus`, `symmetric=False`** | **One consistent shape:** `yerr=` (symmetric) or `ylo=`/`yhi=` (asymmetric). Percent/constant are just array math. |
| **Figure vs Axes vs Axis** name collision (Axes is *singular*, holds two Axis) | **Clear vocabulary** (below): Figure → Subplot → Series; an axis is just `subplot.x` / `subplot.y`. |
| **Requires a DataFrame** (pandas) | **ndarray columns only.** No pandas, ever — group/color-by is a label array, stats come from the stdlib. |

## Faster & safer than what exists — the thesis

The north star: **faster and safer than any plotting library that exists.** Both fall out of leaning on
the cheatah libraries (`ndarray`, `linalg`, `statistics`) and the cheatah-gpu borrow model to their full
advantage, rather than reinventing them.

**Faster** — the whole pipeline is `ndarray` + `linalg` + the GPU borrow model, so nothing walks
per-point:
- data→pixel is one `linalg` affine over a series' `ndarray<Vec2>` (vectorised), never a per-point loop;
- a million-point series is a single `ndarray<Vertex>` the GPU **borrows** (one lease, zero copy) and
  draws in one call;
- an optional `plot.scale`-aware decimation drops sub-pixel points before the vertex build;
- histograms / bins / fits reuse `statistics` / `linalg` (SIMD) kernels, not hand-rolled loops.

**Safer** — the compiler catches what matplotlib/plotly only find at runtime:
- **statically typed** end to end — no `'ro'` / `type='data'` mini-languages, no stringly-typed kwargs;
- **no global state** — a `Figure` is an immutable value (the fluent builder returns copies): no `gca()`
  action-at-a-distance, and figures are trivially reused / parallelised;
- **soft-required parameters** (below) — omitting a param that matters warns at compile time and can be a
  build error, so a rigorous consumer's gate proves every required field was set explicitly;
- **explicit missing-data & bounds** — NaN gaps and axis limits are defined behaviour, not surprises;
- **no-copy borrow ownership** — the GPU never takes your array; a lease pins it and forbids
  mutate/free/move while in use, so the CPU↔GPU seam can't race or dangle.

Matplotlib and plotly are neither at once: interpreted, dict/stringly-typed, globally stateful, copy-happy.

## The one object model (and its vocabulary)

```
Figure                 the whole canvas (one image / one window)
  └─ Subplot             one x/y plotting region  (matplotlib's confusingly-named "Axes")
       ├─ x, y         the two axes — each a plot.scale Range + ticks + label   (no separate "Axis" type)
       └─ Series[]     the data drawn in the subplot: a line / scatter / bar / area / errorbar / hist
```

**A single-subplot figure is the default**, and `figure()` *is* its subplot — so a beginner never has to meet
`Subplot` at all. Grids (`plot.grid(rows, cols)`) opt into multiple subplots only when you want them. This
kills matplotlib's Figure/Axes/Axis confusion: you have a figure, it has series, done — until you ask for
more.

A **Series** is a small typed struct produced by a named constructor (`line`, `scatter`, `bar`, `area`,
`errorbar`, `hist`). There is no stringly-typed `kind='scatter'`; the constructor *is* the kind.

## Two front-ends, one model

The same plot, both ways. **Fluent** (methods return the figure, so they chain; every option is a
default-valued keyword arg — no dicts, no aliases):

```cheatah
import plot
import ndarray

let x = ndarray.linspace(0.0, 6.2832, 200)
let y = ndarray.sin(x)

plot.figure()
    .line(x, y, color = "steelblue", width = 2.0)
    .title("a sine wave")
    .xlabel("t")
    .ylabel("sin t")
    .show()
```

**Declarative** (a typed struct literal — cheatah's `{.field = value}` *is* the "plotly dict", but
statically checked, no `visible=True`, no nesting soup). You **never write a `Fig(` wrapper**: the
parameter's declared type already says it's a `Fig`, so a bare `{…}` is inferred to it — all the
information is there:

```cheatah
plot.figure({
    .series = [ plot.line(x, y, color = "steelblue", width = 2.0) ],
    .title  = "a sine wave",
    .xlabel = "t",
    .ylabel = "sin t",
}).show()
```

Both return a `Figure`; `.show()` / `.save("out.png")` / `.render(w, h)` consume it. The fluent methods
are thin sugar that append to `.series` and set fields on the same spec — so you can start fluent and
drop a fully-specified `Series` in, or build a `Fig` spec and then `.line(...)` more onto it. They never
diverge.

## Error bars — the worked example, done right

```cheatah
# symmetric — one keyword, an array
plot.figure().errorbar(x, y, yerr = e).show()

# asymmetric — ONE consistent naming (lo/hi), no array/arrayminus/symmetric matrix
plot.figure().errorbar(x, y, ylo = lows, yhi = highs).show()

# "percent" / "constant" are not special types — they're just array arithmetic (ndarray)
plot.figure().errorbar(x, y, yerr = y * 0.15).show()      # 15% of each value
plot.figure().errorbar(x, y, yerr = ndarray.full_like(y, 0.1)).show()   # constant 0.1
```

Every trace type follows the same rule: options are named, singular, and default-valued; anything that is
"data shaped" is an ndarray; anything computed (percent, constant, cumulative) is done with the stdlib
before it reaches the plot. No parallel universe of `type=` strings.

## Data model — ndarray columns, never a DataFrame

- **x and y are `ndarray<float>` columns.** That is the whole data model. No `DataFrame`, no pandas, ever.
- **Group / color-by** is an explicit label array: `plot.scatter(x, y, by = species)` where `species` is
  an `ndarray<int>` (or `list<str>`) of category ids the same length as the data. Colors are assigned from
  a palette (`plot.color`). This replaces plotly-express's `color="species"` DataFrame-column magic.
- **Stats transforms are explicit stdlib calls**, not hidden dataframe behaviour:
  - `plot.hist(data, bins = 30)` → bins via `statistics` and draws bars.
  - `plot.figure().line(x, plot.fit(x, y))` → least-squares line via `linalg`.
  - error magnitudes come from `statistics` (std/sem) — you compute the array, you pass it as `yerr`.

If a user wants tabular convenience, that lives *above* cheatah-plot in their own code — the plotting
library stays about arrays of numbers.

## The concrete surface (types)

All numeric data is `ndarray`; the small fixed-size value types are cheatah structs — and now that an
`ndarray` can hold a struct, a **palette is an `ndarray<Color>`** and a **point cloud / vertex buffer is
an `ndarray<Vec2>` / `ndarray<Vertex>`**, so the whole pipeline stays ndarray-native to the GPU seam.

```cheatah
# Fixed-size value types (ndarray-storable).
struct Color  { r : float  g : float  b : float  a : float }   # 0..1; a == 0 means "auto from palette"
struct Vec2   { x : float  y : float }                         # a data point / a pixel position

# One mark drawn in a subplot. Numeric fields are ndarray; style fields default so `line(x,y)` just works.
struct Series {
    kind   : str            # "line"|"scatter"|"bar"|"area"|"errorbar"|"hist" — SET BY THE CONSTRUCTOR
    x      : ndarray<float>
    y      : ndarray<float>
    ylo    : ndarray<float> # error low  (errorbar; empty otherwise)
    yhi    : ndarray<float> # error high
    by     : ndarray<int>   # optional group/colour-by category ids (empty = one group)
    color  : Color          # auto (a == 0) -> next palette colour
    width  : float          # line / bar-edge width (px)
    size   : float          # marker size (px)
    dash   : ndarray<float> # dash pattern [on, off, …]; empty = solid   (ndarray-native, NOT an enum)
    marker : str            # "" = none; else a named shape set by plot.circle / plot.square / …
    label  : str            # legend label
    fill   : bool           # filled (bar/area) vs outline
}

struct Axis    { label : str  scale : Scale  lo : float  hi : float  ticks : int }  # lo/hi NaN = auto
struct Subplot { title : str  x : Axis  y : Axis  series : list<Series>  legend : bool }

# The figure is an immutable value; fluent methods return a modified copy (cheatah `self` is const), and
# `cur` is the subplot subsequent fluent calls target — so single-subplot users never think about it.
struct Figure {
    subplots : list<Subplot>
    rows : int   cols : int   cur : int
    width : int  height : int                 # px
    palette : ndarray<Color>                  # categorical colours (plot.color)
    # ... fluent methods below ...
}
```

## Two ways to build it — the exact signatures

**Named constructors** produce a `Series` (the constructor *is* the kind; options are default-valued —
no aliases, no dicts):

```cheatah
fn line(x, y, color = auto(), width = 1.5, dash = solid(), label = "") -> Series
fn scatter(x, y, color = auto(), size = 6.0, marker = "circle", by = nogroup(), label = "") -> Series
fn bar(x, y, color = auto(), width = 0.8, fill = true, label = "") -> Series
fn area(x, y, color = auto(), label = "") -> Series
fn errorbar(x, y, yerr = none(), ylo = none(), yhi = none(), color = auto(), width = 1.5, label = "") -> Series
fn hist(data, bins = 30, color = auto(), label = "") -> Series      # bins via `statistics`
```

`auto()/solid()/none()/nogroup()` are tiny helpers returning the sentinel defaults (a transparent
`Color`, an empty `ndarray`) — cheatah lets a default arg be a constructor call, and omitted struct
fields auto-default, so this is all native.

**Fluent** methods on `Figure` mirror the constructors and append to the current subplot, plus
axis/label/legend setters — each returns a new `Figure`:

```cheatah
fn line(self, x, y, color = auto(), width = 1.5, dash = solid(), label = "") -> Figure
fn scatter(self, …) -> Figure     fn bar(self, …) -> Figure     fn errorbar(self, …) -> Figure   # etc.
fn add(self, s : Series) -> Figure                 # drop in a pre-built Series (bridges the two styles)
fn title(self, t) -> Figure
fn xlabel(self, s) -> Figure       fn ylabel(self, s) -> Figure
fn xlim(self, lo, hi) -> Figure    fn ylim(self, lo, hi) -> Figure
fn xscale(self, s : Scale) -> Figure                # plot.linear (default) or plot.log
fn legend(self, on = true) -> Figure
fn subplot(self, r, c) -> Figure                    # target a cell of a grid for subsequent calls
fn show(self)                                       # a window        [roadmap: plot.window]
fn save(self, path)                                 # encode to a file [roadmap: plot.renderer]
fn render(self, w, h) -> ndarray<Color>             # offscreen pixels (host readback) — the stream frame
```

**Declarative spec** — the same `Figure`, described as a typed struct literal (the "plotly dict", checked):

```cheatah
struct Fig {
    series : list<Series>   title : str   xlabel : str   ylabel : str
    xscale : Scale   yscale : Scale   legend : bool   width : int   height : int
    subplots : list<Subplot>          # for grids; omit for a single subplot
}
fn figure(spec : Fig = {}) -> Figure   # call it bare: plot.figure({.series = […], .title = "…"})
```

Top-level conveniences (so `import plot` alone draws): `plot.line(x,y)…`, `plot.scatter`, `plot.hist`
each return a one-series `Figure`; `plot.grid(rows, cols)` returns a multi-subplot `Figure`.

## Style without enums — colours, lines, markers

cheatah has no enums, and we refuse stringly-typed magic — so style values are **typed** (structs +
module constants + ndarray), never bare strings the user has to memorise:

- **Colour** is the `Color` struct. Get one by `plot.rgb(0.2,0.4,0.8)`, `plot.rgba(…)`, or
  `plot.named("steelblue")` (a small CSS-ish table). A **palette is `ndarray<Color>`**:
  `plot.palette("tab10")`, and a continuous map samples to a colour: `plot.viridis(t)`.
- **Line style is data, not a code**: `dash` is an `ndarray<float>` pattern (`[]` solid, `[4,2]` dashed) —
  no `'--'` mini-language, no `LineStyle.dashed` enum. `width` is a float.
- **Marker shape** is a named constant that sets `marker`: `plot.circle`, `plot.square`, `plot.diamond`
  (each just a value) — so you write `scatter(x, y, marker = plot.circle)`, never a raw `"o"`.

## Scales & axes

`plot.scale` (done) is the linear seed; a `Scale` is a small struct with a transform, chosen by module
constant — `plot.linear` (default), `plot.log`, later `plot.symlog`/`plot.time`. No enum:

```cheatah
plot.line(x, y).yscale(plot.log).ylim(1.0, 1000.0).show()
```

Axes carry `label`, `lo`/`hi` limits (auto when NaN), a target tick count, and get "nice" ticks from
`plot.scale.ticks`. Categorical x (bar charts) maps category ids → evenly spaced positions with string
tick labels.

## Layout & subplots (only when you ask)

```cheatah
plot.grid(1, 2)                        # a 1×2 figure
    .subplot(0, 0).line(t, sig).title("signal")
    .subplot(0, 1).hist(sig, bins = 40).title("distribution")
    .show()
```

Single-subplot is the default and never mentions `Subplot`. Grids add shared-axis and spacing options on
the `Figure` spec.

## Group / colour-by (the no-pandas faceting)

```cheatah
plot.scatter(petal_len, petal_wid, by = species)     # species : ndarray<int> of category ids
    .legend()
    .show()
```

`by` assigns `palette[species[i]]` per point and one legend entry per category — replacing
plotly-express's `color="species"` DataFrame-column magic with an explicit label array + an
`ndarray<Color>` palette.

## Output & streaming — where the borrow model lands

`show()` opens a GLFW window; `save(path)` encodes a file. The streaming path is `render(w, h)`, which
draws into an **offscreen** target and reads it back to a host **`ndarray<Color>`** — the GPU never
copies your data, it **borrows** the vertex `ndarray` and the readback target via a lease (see the render
seam below). So a live plot streams frame-by-frame with no JavaScript:

```cheatah
# a live plot streamed to a socket, no JS — each frame is a borrowed host pixel array
loop {
    let frame = plot.line(t, latest(t)).render(1280, 720)   # ndarray<Color>, host-owned
    send(encode_png(frame))
}
```

## The full mark catalog

Every mark is a named constructor returning a `Series` (the kind is set for you) with default-valued
options. Adding one is a constructor + a branch in the renderer's reduction — nothing else changes.

| constructor | what | data | v1 |
|---|---|---|:--:|
| `line(x, y)` | polyline | x, y | ✓ |
| `scatter(x, y)` | points / markers | x, y (+ `by` / `c`) | ✓ |
| `bar(x, y)` | bars (numeric or categorical x) | x, y | ✓ |
| `area(x, y)` | filled area under a curve | x, y | ✓ |
| `fill_between(x, ylo, yhi)` | band between two curves (CI / min–max) | x, ylo, yhi | ✓ |
| `step(x, y)` | staircase | x, y | ✓ |
| `stem(x, y)` | stems + heads | x, y | ✓ |
| `errorbar(x, y, …)` | points/line + whiskers | x, y, ylo, yhi | ✓ |
| `hist(data, bins)` | histogram | data | ✓ |
| `heatmap(z)` / `image(z)` | 2-D field → coloured image | z: `ndarray<float>` H×W | ✓ |
| `box(groups)` | box-and-whisker | list of series | roadmap |
| `violin(groups)` | KDE density | data | roadmap |
| `contour(z)` | iso-lines | z | roadmap |
| `quiver(x, y, u, v)` | vector field | ndarray | roadmap |

## Colour beyond categories — colormaps & the heatmap

Two kinds of colour, both ndarray-native:
- **Categorical** (a class per series/point) → the palette is an `ndarray<Color>` (above).
- **Continuous** (a value → a colour) → a **colormap** `fn cmap(t : float) -> Color` backed by an
  `ndarray<Color>` LUT (`plot.viridis`, `plot.magma`, `plot.coolwarm`). `scatter(x, y, c = z, cmap =
  plot.viridis)` colours points by the `z` array and auto-adds a colourbar.

A **heatmap / image** is the purest ndarray case: `heatmap(z)` takes a 2-D `ndarray<float>`, normalises it
(vectorised) and maps it through a colormap to an **`ndarray<Color>` image** — which the GPU borrows as a
**texture** under the exact same lease model as a vertex buffer. `image(rgba)` takes an `ndarray<Color>`
directly. No per-pixel loops; the colormap is one `ndarray` gather.

## Text, labels & automatic layout

Text is what most libraries botch (matplotlib's `tight_layout`/`constrained_layout` dance). Here:
- Glyphs come from a **font atlas** the renderer builds once; a text run becomes textured quads (the
  render seam's "text runs").
- **Layout is computed from measured text.** Before drawing, the renderer measures tick-label, axis-label,
  title and legend extents from atlas metrics and derives the plot-area rectangle + margins so nothing
  clips and nothing is hand-tuned — one deterministic pass, no retries.
- Text **metrics are CPU-side and pure** (only glyph *rasterisation* needs the GPU), so the whole layout
  algorithm is testable without a device — like `plot.scale` today.

## Ticks — locators & formatters (pure functions, no magic strings)

Ticks are two composable pure functions over `ndarray`:
- **Locator** — where ticks go: `plot.scale.ticks` (nice 1/2/5, default), `plot.log_ticks`,
  `plot.time_ticks`. Returns an `ndarray<float>` of positions.
- **Formatter** — how each reads: `fn(x : float) -> str` chosen by module constant — `plot.fmt.number`
  (default), `.scientific`, `.percent`, `.si` (k/M/G), `.time(fmt)`.

`xticks(plot.log_ticks, plot.fmt.si)` is explicit and typed — never
`ax.xaxis.set_major_formatter(FuncFormatter(…))`.

## Themes & defaults — a value, not an rcParams soup

A `Theme` bundles the global look — background, grid colour/visibility, font, default palette, default
line width — as one struct applied to a `Figure` (or set as the process default). The **default theme is
colourblind-safe** and light; `plot.theme.dark` / `plot.theme.minimal` are presets.

```cheatah
plot.line(x, y).theme(plot.theme.dark).show()
plot.default_theme(plot.theme.minimal)     # everything after uses it
```

## Reference lines, spans & annotations

Decorations that aren't data series, added fluently or in the spec, positioned in **data coordinates**
(mapped through the same `linalg` affine as the series, so they track the axes):
- `hline(y)` / `vline(x)` — a threshold, a mean;
- `hspan(y0, y1)` / `vspan(x0, x1)` — a shaded band (a confidence region, a regime);
- `annotate(x, y, text)` / `arrow(x0, y0, x1, y1)` — callouts.

## Missing data (NaN / inf) — explicit, never silent

A `NaN` in `y` splits a line into **gaps**, a `NaN` point is skipped by scatter, and `NaN`/`inf` are
excluded from auto-limits — all stated and testable on the pure geometry, so a dataset with holes plots
sanely instead of spiking to zero.

## Large data & performance

cheatah is performance-first, and the ndarray + borrow model is why big plots stay fast:
- A million-point series is one `ndarray<Vertex>` the GPU **borrows** (one lease, no copy) and draws in a
  single call — the CPU never walks per-point.
- A line denser than the pixels gets an optional **decimation** pass (`plot.scale`-aware min/max per pixel
  column over the `ndarray`, via `linalg`) *before* the vertex build — vectorised, not a loop.
- Nothing on the hot path allocates per-point: it is matrix×array plus one borrowed buffer.

## Save formats — raster and vector, two honest paths

- **Raster** (`save("p.png")`, `render(w, h)`): draw offscreen, **read back** the `ndarray<Color>` target,
  encode. Pixel-exact; this is what streaming uses.
- **Vector** (`save("p.svg")`, later PDF): the renderer's **draw list** (polylines, quads, text runs — the
  same reduction, minus rasterisation) serialises straight to SVG — no GPU, resolution-independent.

One reduction, two backends: the draw list is the seam — raster rasterises it, vector serialises it.

## More worked examples

```cheatah
# fluent line + declarative equivalent already shown above. A few more:

# asymmetric error bars (the plotly worked example), done once, consistently:
plot.figure().errorbar(x, y, ylo = lows, yhi = highs).title("±").show()

# a histogram from raw samples (binned via `statistics`):
plot.hist(samples, bins = 50).xlabel("value").ylabel("count").show()

# a least-squares trend over a scatter (fit via `linalg`):
plot.figure()
    .scatter(x, y, size = 4.0)
    .line(x, plot.fit(x, y), color = plot.named("crimson"), label = "fit")
    .legend()
    .show()
```

## Open design choices (to confirm as we build)

- **Marker set & names** (`plot.circle/square/diamond/…`) — which shapes ship first.
- **Named-colour table** scope (a handful vs a full CSS set) and default **palette** (`tab10`-like?).
- **`render()` pixel type**: `ndarray<Color>` (float RGBA, clean) vs a packed `ndarray<u8x4>` (smaller,
  encode-ready) — a borrow-model + memory question that pairs with the easy `gpu` readback format.

## Module map (mostly `.purr`, on `linalg` / `ndarray` / `statistics` / `random`)

| module | what | deps | status |
|---|---|---|---|
| `plot.scale` | data↔pixel, "nice" ticks (pure geometry) | ndarray, math | **done** |
| `plot.color` | typed colors, palettes, categorical color assignment | (random for qualitative palettes) | new |
| `plot.series` | the `Series` structs + named constructors (line/scatter/bar/area/errorbar/hist) | ndarray | new |
| `plot.stats` | histogram binning, error helpers, least-squares fit, density | statistics, linalg | new |
| `plot.figure` | `Figure`/`Subplot` model, the fluent builder + the `Fig` spec, layout/legend (uses `plot.scale`) | ndarray | new |
| `plot.renderer` | `Figure` → 2D draw primitives → easy `gpu` offscreen target → present/readback | **cheatah-gpu** | roadmap |
| `plot.window` | GLFW surface behind a thin interface + shim | GLFW (C shim) | roadmap |
| `plot` (umbrella) | the easy entry: `figure()`, top-level `line/scatter/hist/...`, `show/save` | aggregates the above | new |

Everything except `plot.renderer`/`plot.window` (and the tiny GLFW shim) is **pure cheatah on the
stdlib** — no GPU needed to build, test, and document it, exactly like `plot.scale` today.

## The render seam → what the easy `gpu` API must provide (the next step)

The renderer reduces *any* `Figure` to a tiny, backend-agnostic set of 2D primitives, drawn into an
**offscreen** target that is then **presented to a window** or **read back to host** (the no-JS streaming
path). That reduction is the contract that tells cheatah-gpu what its easy layer needs.

The reduction is **vectorized on ndarray + linalg**, never a per-point loop: each series' data→pixel map
is a single **`linalg` affine** (a 2×3 transform built from the per-axis linear maps `plot.scale`
provides) applied to the series' `ndarray<Vec2>`; markers/bars/glyphs are then instanced from those
transformed points. So the hot path is a handful of matrix-times-array ops, and its output is the
`ndarray<Vertex>` the GPU borrows.

The renderer's output is itself ndarray-native: it fills an **`ndarray<Vertex>`** (a `Vertex` being a
fixed-size POD struct — pixel position, RGBA colour, UV) plus a small draw list. Per cheatah-gpu's
**no-copy borrow model** (`cheatah-gpu/docs/DESIGN.md` §"Concurrency & memory ownership"), the GPU does
**not** copy or "upload" that array — a **GPU lease** hands the backend a *non-owning view* of it, pins it
(the user keeps ownership), and a mutex on the CPU↔GPU interface keeps it from being mutated/freed/moved
while the GPU reads it. So the vertex `ndarray` the renderer builds is *borrowed*, not uploaded.

Primitives the renderer emits (pixel-space, as `Vertex` runs + a draw list):
- **stroked polylines** — series lines, axes, gridlines (width, color, optional dash)
- **filled convex shapes / quads** — bars, area fills, legend swatches, marker glyphs
- **points / markers** — instanced small quads (circle/square/…), color + size
- **text runs** — glyphs from an atlas → textured quads (tick labels, axis labels, legend, title)
- **clip rect** — restrict drawing to a subplot

So the **easy `gpu` API (cheatah-gpu) must offer**, at minimum:
1. a **headless-capable device/context**;
2. an **offscreen RGBA8 color target** of W×H, with a render pass + clear color;
3. a **2D draw surface** that **borrows** an `ndarray<Vertex>` via a lease (no copy) and draws
   triangles/lines from it — with a **pixel-space ortho** transform and **alpha blending**; two pipelines
   suffice (solid-color, and textured for glyphs);
4. **present** the target to a swapchain (window) **OR** **read back** its pixels into a host
   `ndarray` (again a borrowed, ownership-retained target) — the no-JS streaming path.

That is deliberately small — a 2D drawing surface + offscreen + present/readback, all over the borrow
model.

**This easy `gpu` layer does not exist yet — it is the THIRD cheatah-gpu API we have to create.**
cheatah-gpu ships two 1:1 native surfaces today, `gpu.vulkan` and `gpu.metal`; the easy `gpu` layer is a
new, slim, cross-platform tier **built on top of them — it composes their forwarders and never
reimplements a native call**, so every native call keeps exactly one implementation (covered by the 1:1
device-matrix tests) and the easy layer adds only the orchestration: the borrow-model lease, the
offscreen target, the pixel-space 2D pipeline, and present/readback. cheatah-plot is its **first real
consumer**, and this render seam is precisely the surface it must expose — which is why we pinned the
plotting API down first. Build order:

1. lock this `import plot` model — pure cheatah on `ndarray`/`linalg`/`statistics`, fully testable now;
2. grow the easy `gpu` layer in cheatah-gpu **to this seam**, composing `gpu.vulkan` / `gpu.metal`;
3. fill in `plot.renderer` / `plot.window` against it.

## What we build first (the `import plot` mechanism)

1. `plot.series` — the `Series` struct + `line`/`scatter`/`bar`/`errorbar`/`hist` constructors (pure).
2. `plot.figure` — `Figure`/`Subplot`, the fluent builder methods, and the `Fig` spec form; layout math on
   `plot.scale`. `.render()`/`.show()`/`.save()` are present but call into the roadmap renderer (a stub
   that errors clearly until `plot.renderer` lands on the easy `gpu` layer).
3. `plot` umbrella — re-export `figure()` + top-level `line/scatter/hist/...` conveniences so
   `import plot` gives the whole surface.

All three are `.purr`, gated by the existing QA machinery (systests + C++ coverage + docs), so the API
model is locked and tested *before* any pixels exist — then the renderer is filled in against the easy
`gpu` layer we design from the seam above.
