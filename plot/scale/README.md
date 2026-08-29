# plot.scale

Axis geometry: turn a range of data into tick positions and pixel coordinates. Pure cheatah on
`ndarray` — no device, no figure, nothing to set up.

```purr
import plot.scale as scale
import ndarray

let values = ndarray.array([3.2, 7.9, 5.1, 4.4])
let r      = scale.data_range(values)      # Range 3.2 .. 7.9
let ticks  = scale.ticks(r, 5)             # round tick values inside that range
let px     = scale.to_pixel(5.1, r, 640)   # where 5.1 lands on a 640px axis
```

> **Status:** working

Log axes are the same shape: `log_ticks` produces decade ticks with 1-2-5 subdivision, and
`to_pixel_log` places a value on them. A range whose data is empty or degenerate falls back to
`0 .. 1` rather than producing a division by zero, so an axis over no data still draws.
