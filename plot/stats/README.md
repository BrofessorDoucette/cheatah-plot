# plot.stats

The statistics a plot needs before it can draw: histogram binning, least-squares fit lines, and
standard error. Built on `ndarray`, `linalg` and `statistics` rather than reimplementing them.

```purr
import plot.stats as stats
import ndarray

let samples = ndarray.array([1.0, 1.4, 2.2, 2.3, 2.9, 3.1, 3.4])
let h       = stats.histogram(samples, 5)    # 5 bins: edges and counts
let trend   = stats.fit(xs, ys)              # least-squares trend line, via linalg.lstsq
```

> **Status:** working

The fit is a real least-squares solve through `linalg.lstsq`, not a hand-rolled normal equation, so
it degrades the way the rest of the numeric stack does on ill-conditioned input.
