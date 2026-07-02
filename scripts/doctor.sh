#!/usr/bin/env bash
# doctor.sh — the preflight HOOK biome runs to check a machine can build + run cheatah-plot.
#
# Prefer `biome doctor` (biome invokes this via cheatah.toml's `doctor = "scripts/doctor.sh"`); run this
# script directly only as a fallback. It verifies the userspace stack and tells you EXACTLY how to fix any
# gap — usually `biome add cheatah-plot` (which provisions everything) or, bare-metal, its install hook
# scripts/install-deps.sh. Checks:
#   1. GLFW is present                                        — required (plotting owns a window)
#   2. the GPU backend the platform uses:
#        • macOS — Apple's NATIVE Metal (built in; no MoltenVK/Vulkan)
#        • Linux — the Vulkan loader + an enumerable Vulkan device (the driver/ICD)
#   3. `slangc` compiles shaders/hello.slang to that backend's target (Metal on macOS, SPIR-V on Linux)
#
# Exit code 0 = ready; non-zero = a required component is missing. This is the convention a future
# `biome doctor` will run after install.
set -uo pipefail
cd "$(git rev-parse --show-toplevel 2>/dev/null || dirname "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)")"

ok()   { printf '  \033[32m✓\033[0m %s\n' "$*"; }
bad()  { printf '  \033[31m✗\033[0m %s\n' "$*"; }
warn() { printf '  \033[33m!\033[0m %s\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }

status=0
echo "cheatah-plot doctor — checking the plotting userspace stack:"

# 1. GLFW — required (cheatah-plot's default windowing backend) --------------------------------
if pkg-config --exists glfw3 2>/dev/null || ls /usr/lib/*/libglfw* /usr/local/lib/libglfw* >/dev/null 2>&1; then
    ok "GLFW present"
else
    bad "GLFW not found — cheatah-plot's window backend. Run: scripts/install-deps.sh"; status=1
fi

# 2. GPU backend — Metal on macOS (native), Vulkan on Linux ------------------------------------
# cheatah-plot renders through cheatah-gpu's easy `gpu` layer, which is Apple's NATIVE Metal on macOS
# (no MoltenVK/Vulkan) and Vulkan on Linux. Check the backend the platform actually uses.
if [ "$(uname -s)" = "Darwin" ]; then
    ok "Metal backend: native (built into macOS)"
    SLANG_TARGET=metal; SLANG_LABEL="Metal (Apple)"
else
    loader_present() {
        ldconfig -p 2>/dev/null | grep -q 'libvulkan\.so' && return 0
        for f in /usr/lib/libvulkan.so* /usr/lib/*/libvulkan.so* /usr/local/lib/libvulkan.so* /lib/*/libvulkan.so*; do
            [ -e "$f" ] && return 0
        done
        return 1
    }
    if loader_present; then
        ok "Vulkan loader present"
    else
        bad "Vulkan loader (libvulkan) not found — run: scripts/install-deps.sh"; status=1
    fi
    # The real driver/ICD test: a Vulkan-capable device must be enumerable.
    if have vulkaninfo; then
        if dev=$(vulkaninfo --summary 2>/dev/null | grep -m1 -E 'deviceName' | sed 's/.*= //'); then
            [ -n "$dev" ] && ok "Vulkan device: $dev" || { bad "vulkaninfo ran but enumerated no device — install a GPU driver (Mesa/NVIDIA/AMD)"; status=1; }
        else
            bad "vulkaninfo found no Vulkan device — install/enable a GPU driver (Mesa/NVIDIA/AMD)"; status=1
        fi
    else
        warn "vulkaninfo not installed — can't confirm a device. Run: scripts/install-deps.sh"; status=1
    fi
    SLANG_TARGET=spirv; SLANG_LABEL="SPIR-V (Vulkan)"
fi

# 3. Slang toolchain: shaders/hello.slang must compile to the platform's shader target ---------
if have slangc; then
    tmp="$(mktemp -d)"
    if slangc shaders/hello.slang -target "$SLANG_TARGET" -entry main -o "$tmp/hello.out" >"$tmp/slang.log" 2>&1; then
        if [ "$SLANG_TARGET" = "spirv" ] && have spirv-val && spirv-val "$tmp/hello.out" >/dev/null 2>&1; then
            ok "slangc: shaders/hello.slang -> valid SPIR-V (Vulkan)"
        else
            ok "slangc: shaders/hello.slang -> $SLANG_LABEL"
        fi
    else
        bad "slangc failed on shaders/hello.slang:"; sed 's/^/      /' "$tmp/slang.log"; status=1
    fi
    rm -rf "$tmp"
else
    bad "slangc not found — needed to build Slang shaders. See: scripts/install-deps.sh"; status=1
fi

echo
if [ "$status" -eq 0 ]; then
    printf '\033[32mcheatah-plot: ready to plot.\033[0m\n'
else
    printf '\033[31mcheatah-plot: not ready — fix the ✗ items above (usually: scripts/install-deps.sh).\033[0m\n'
fi
exit "$status"
