# plot.color

Colours and palettes: named CSS-style colours, the perceptually-uniform colormaps
(`viridis`, `magma`, `coolwarm`), and the interpolation that samples them.

```purr
import plot.color as color

let c    = color.named("steelblue")     # a Color, by the name you already know
let warm = color.viridis(0.75)          # sample a colormap at t in [0, 1]
let pal  = color.palette(6)             # 6 distinguishable series colours
```

> **Status:** working

Colours are plain values, so a series colour, a heatmap sample and a background are the same type —
there is no separate "colormap object" to construct and thread through a call chain.
