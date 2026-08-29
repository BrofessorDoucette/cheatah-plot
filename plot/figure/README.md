# plot.figure

The figure itself: axes, subplot grids, titles, legends — the container the marks live in, and what
you hand to `plot.save`.

```purr
import plot
import ndarray

let xs  = ndarray.array([0.0, 1.0, 2.0, 3.0])
let ys  = ndarray.array([0.0, 1.0, 4.0, 9.0])
let fig = plot.line(plot.new_figure(), xs, ys)
plot.save(plot.title(fig, "y = x²"), "parabola.png")
```

> **Status:** working

The builders are free functions returning a modified copy, so a figure is composed by chaining
rather than by mutating a handle — `plot.title(plot.line(fig, xs, ys), "…")` is the whole idiom, and
an intermediate figure stays valid if you keep it.
