// plot.slang — THE kernel source of the cheatah-plot renderer: both compute entry points,
// written once in Slang and compiled per backend at build time (CMakeLists.txt):
//   - Vulkan: slangc -target spirv, one .spv per entry point (entry renamed "main" per module);
//   - Metal:  slangc -target metal, one .metal per entry (Apple compiles it at runtime; the
//             off-Apple software-emulated Metal device ignores shader source and runs the C++
//             stand-ins registered in plot/renderer/gpu_context.hpp instead).
//
// Each entry is guarded (-DKERNEL_plot_clear / -DKERNEL_plot_raster) so every compiled module
// carries exactly one entry point and its own bindings, the proven per-entry compilation scheme.
//
// THE SPEC of every expression below is plot/renderer/raster_cpu.hpp — the reference rasterizer.
// The coverage functions are ITS float expressions transcribed 1:1 (same 0.5-pixel feathers, same
// saturate), and the blend is the SAME integer src-over arithmetic, so the only cross-backend
// wiggle is sqrt/floor ULP noise inside the feather window (the tests bound it per channel).
//
// ABI (shared with plot/renderer/kernels.hpp — keep in sync):
//   - `Prim` is 16 four-byte lanes (64 bytes): uint type, uint rgba, float f0..f9, uint u0..u3 —
//     byte-identical to the C++ POD (the fixed field names f0..f9/u0..u3 flatten its arrays).
//   - The framebuffer is one packed RGBA8 uint per pixel, row-major (r | g<<8 | b<<16 | a<<24).
//   - tile_offsets/tile_prims are the CSR tile bins of binning.hpp; a tile is 16x16 pixels.
//   - The LAST buffer of every kernel is a uint params buffer: {width, height, tiles_x, clear}.
//   - Both entries use [numthreads(16, 16, 1)] — one thread per pixel, guarded beyond
//     width/height, so a rounded-up dispatch is safe.

/// One drawable primitive — the Slang spelling of kernels.hpp's 64-byte `Prim` POD. The f lanes
/// are per-type geometry, the u lanes carry a glyph's 8x16 bitmap (4 rows per lane).
struct Prim {
    uint type;    // the PrimType code: 0 seg, 1 disc, 2 rect, 3 tri, 4 glyph
    uint rgba;    // packed draw colour
    float f0; float f1; float f2; float f3; float f4;
    float f5; float f6; float f7; float f8; float f9;
    uint u0; uint u1; uint u2; uint u3;
};

// Coverage of pixel center (px, py) by a stroked, optionally dashed segment: 1px feather on the
// distance to the segment, dash phase measured along it (raster_cpu.hpp detail::cov_seg).
float cov_seg(Prim p, float px, float py) {
    const float ax = p.f0, ay = p.f1, bx = p.f2, by = p.f3;
    const float dx = bx - ax, dy = by - ay;
    const float len2 = dx * dx + dy * dy;
    float t = 0.0f;
    if (len2 > 0.0f) t = saturate(((px - ax) * dx + (py - ay) * dy) / len2);
    const float cx = ax + t * dx, cy = ay + t * dy;
    const float d = sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
    float cov = saturate(p.f4 + 0.5f - d);
    const float on = p.f5, off = p.f6;
    if (on > 0.0f && off > 0.0f) {
        const float along = t * sqrt(len2);
        const float phase = along - floor(along / (on + off)) * (on + off);
        if (phase >= on) cov = 0.0f;
    }
    return cov;
}

// Coverage by a filled disc: 1px feather on the radial distance (detail::cov_disc).
float cov_disc(Prim p, float px, float py) {
    const float d = sqrt((px - p.f0) * (px - p.f0) + (py - p.f1) * (py - p.f1));
    return saturate(p.f2 + 0.5f - d);
}

// Coverage by an axis-aligned rect — filled (exact box AA), or an outline band when f4 > 0
// (feathered distance to the box boundary) (detail::cov_rect).
float cov_rect(Prim p, float px, float py) {
    const float x0 = p.f0, y0 = p.f1, x1 = p.f2, y1 = p.f3;
    if (p.f4 > 0.0f) {
        const float mx = max(x0 - px, px - x1);
        const float my = max(y0 - py, py - y1);
        const float outside = sqrt(max(mx, 0.0f) * max(mx, 0.0f) +
                                   max(my, 0.0f) * max(my, 0.0f));
        const float inside = max(mx, my);               // negative depth when inside
        const float d = outside > 0.0f ? outside : inside;
        return saturate(p.f4 + 0.5f - abs(d));
    }
    const float cx = saturate(min(px + 0.5f, x1) - max(px - 0.5f, x0));
    const float cy = saturate(min(py + 0.5f, y1) - max(py - 0.5f, y0));
    return cx * cy;
}

// One signed inward edge distance of the triangle coverage (the lambda in detail::cov_tri).
float tri_edge(float sgn, float px, float py, float ex0, float ey0, float ex1, float ey1) {
    const float ex = ex1 - ex0, ey = ey1 - ey0;
    const float elen = sqrt(ex * ex + ey * ey);
    if (elen == 0.0f) return -1.0f;
    return sgn * ((px - ex0) * ey - (py - ey0) * ex) / elen;    // signed inward distance
}

// Coverage by a filled triangle: 1px feather on the min signed edge distance (winding normalized
// by the doubled signed area) (detail::cov_tri).
float cov_tri(Prim p, float px, float py) {
    const float x0 = p.f0, y0 = p.f1, x1 = p.f2, y1 = p.f3, x2 = p.f4, y2 = p.f5;
    const float area2 = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (area2 == 0.0f) return 0.0f;
    const float sgn = area2 > 0.0f ? 1.0f : -1.0f;
    const float d0 = tri_edge(sgn, px, py, x0, y0, x1, y1);
    const float d1 = tri_edge(sgn, px, py, x1, y1, x2, y2);
    const float d2 = tri_edge(sgn, px, py, x2, y2, x0, y0);
    return saturate(0.5f - max(d0, max(d1, d2)));
}

// Coverage by an 8x16 glyph: a pixel-aligned bitmap test, no AA (detail::cov_glyph). The lane
// select is a branch over the four fixed u fields — the C++ side's u[gy / 4].
float cov_glyph(Prim p, float px, float py) {
    const int gx = int(floor(px - p.f0));
    const int gy = int(floor(py - p.f1));
    if (gx < 0 || gx >= 8 || gy < 0 || gy >= 16) return 0.0f;
    uint lane = p.u0;
    if (gy >= 12) lane = p.u3;
    else if (gy >= 8) lane = p.u2;
    else if (gy >= 4) lane = p.u1;
    const uint row = (lane >> uint(8 * (gy % 4))) & 0xFFu;
    return ((row >> uint(7 - gx)) & 1u) != 0u ? 1.0f : 0.0f;
}

// Coverage dispatch over the primitive kinds (detail::coverage).
float coverage(Prim p, float px, float py) {
    switch (p.type) {
        case 0u: return cov_seg(p, px, py);
        case 1u: return cov_disc(p, px, py);
        case 2u: return cov_rect(p, px, py);
        case 3u: return cov_tri(p, px, py);
        case 4u: return cov_glyph(p, px, py);
        default: break;
    }
    return 0.0f;
}

// The ONE quantization from float coverage to the integer blend (detail::quantize_cov): the
// uint() conversion truncates, exactly like the C++ static_cast.
uint quantize_cov(float cov) {
    return uint(saturate(cov) * 255.0f + 0.5f);
}

// Integer src-over: blend src (packed RGBA8, its alpha scaled by cov8) over dst, with the exact
// (x + 127) / 255 rounding of detail::blend_over — pure uint arithmetic, bit-stable everywhere.
uint blend_over(uint dst, uint src, uint cov8) {
    const uint sa = ((src >> 24) & 0xFFu) * cov8 / 255u;    // effective src alpha 0..255
    if (sa == 0u) return dst;
    uint outp = 0u;
    [unroll]
    for (int ch = 0; ch < 3; ++ch) {
        const uint s = (src >> uint(8 * ch)) & 0xFFu;
        const uint d = (dst >> uint(8 * ch)) & 0xFFu;
        const uint o = (s * sa + d * (255u - sa) + 127u) / 255u;
        outp |= o << uint(8 * ch);
    }
    const uint da = (dst >> 24) & 0xFFu;
    const uint oa = sa + da * (255u - sa) / 255u;
    return outp | (oa << 24);
}

#ifdef KERNEL_plot_clear
// plot_clear: one thread per pixel, writes params.clear into the RGBA8 framebuffer.
// Buffers: {framebuffer, params}.
[[vk::binding(0, 0)]] RWStructuredBuffer<uint> clear_fb;
[[vk::binding(1, 0)]] StructuredBuffer<uint>   clear_params;

[shader("compute")]
[numthreads(16, 16, 1)]
void plot_clear(uint3 tid : SV_DispatchThreadID) {
    const uint width = clear_params[0], height = clear_params[1];
    if (tid.x >= width || tid.y >= height) return;
    clear_fb[tid.y * width + tid.x] = clear_params[3];
}
#endif  // KERNEL_plot_clear

#ifdef KERNEL_plot_raster
// plot_raster: one thread per pixel — compute its tile, walk that tile's CSR prim list in paint
// order, accumulate coverage-scaled integer src-over from the clear colour, store once.
// Buffers: {prims, tile_offsets, tile_prims, framebuffer, params}.
[[vk::binding(0, 0)]] StructuredBuffer<Prim>   raster_prims;
[[vk::binding(1, 0)]] StructuredBuffer<uint>   raster_tile_offsets;
[[vk::binding(2, 0)]] StructuredBuffer<uint>   raster_tile_prims;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> raster_fb;
[[vk::binding(4, 0)]] StructuredBuffer<uint>   raster_params;

[shader("compute")]
[numthreads(16, 16, 1)]
void plot_raster(uint3 tid : SV_DispatchThreadID) {
    const uint width = raster_params[0], height = raster_params[1];
    if (tid.x >= width || tid.y >= height) return;
    const uint tiles_x = raster_params[2];
    const uint tile = (tid.y / 16u) * tiles_x + (tid.x / 16u);    // kTile == 16
    const float px = float(tid.x) + 0.5f;
    const float py = float(tid.y) + 0.5f;
    uint pixel = raster_params[3];                                // start at the clear colour
    const uint end = raster_tile_offsets[tile + 1u];
    for (uint k = raster_tile_offsets[tile]; k < end; ++k) {
        const Prim p = raster_prims[raster_tile_prims[k]];
        const uint cov8 = quantize_cov(coverage(p, px, py));
        if (cov8 != 0u) pixel = blend_over(pixel, p.rgba, cov8);
    }
    raster_fb[tid.y * width + tid.x] = pixel;
}
#endif  // KERNEL_plot_raster
