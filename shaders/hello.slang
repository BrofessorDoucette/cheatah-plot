// hello.slang — a minimal Slang compute shader (OUTLINE / smoke artifact).
//
// This is the "it works" shader: scripts/doctor.sh compiles it with `slangc` to SPIR-V and validates it
// with `spirv-val`, proving the Slang -> SPIR-V toolchain is healthy end-to-end before any real GPU
// work. It doubles as the template the plot renderer's real shaders start from.
//
// Build manually:
//   slangc shaders/hello.slang -target spirv -entry main -o hello.spv && spirv-val hello.spv
//
// It fills an output buffer with f(i) = i * 2 — a stand-in for a real cheatah-plot kernel (e.g. mapping
// data values to pixel positions). The [numthreads(64,...)] local size pairs with a host-side
// group_count_1d(n, 64) dispatch on cheatah-gpu's easy `gpu` layer.

RWStructuredBuffer<uint> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    output[tid.x] = tid.x * 2u;
}
