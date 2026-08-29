# plot.series

The marks a figure draws — lines, points, bars, areas, steps, error bars, heatmaps, glyphs. A
series is a value: build it, hand it to a figure, and it renders.

```purr
import plot.series as series
import ndarray

let xs = ndarray.array([0.0, 1.0, 2.0, 3.0])
let ys = ndarray.array([0.0, 1.0, 4.0, 9.0])
let s  = series.line(xs, ys)            # a line mark over those points
let p  = series.scatter(xs, ys)         # the same data as points
```

> **Status:** working

Every mark kind has a two-argument constructor and a fuller one, rather than default arguments:
the short form is what you reach for, the long form carries style. Both return a new value, so a
series is never mutated behind your back.
