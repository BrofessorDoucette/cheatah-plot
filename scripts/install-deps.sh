#!/usr/bin/env bash
# install-deps.sh — the OS-package provisioning HOOK that biome runs for cheatah-plot.
#
# You almost certainly do NOT run this by hand. Provision through the package manager instead:
#
#     biome add cheatah-plot     # fetches cheatah-plot + cheatah-gpu AND provisions this stack
#
# biome reads cheatah.toml's [system-dependencies] and invokes this script (declared there as
# `install = "scripts/install-deps.sh"`) to install the per-platform packages. Keeping the provisioning
# in biome is the whole point: one manifest, one command, nothing bespoke to rebuild per project. This
# script lives here only so biome has something concrete to call (and as a manual escape hatch if you are
# bootstrapping a machine WITHOUT a cheatah install yet). The package lists below are the OS-specific
# expansion of what cheatah.toml already declares — edit the manifest, not just this script.
#
# What it installs — the *userspace* stack cheatah-plot needs (never kernel/GPU drivers: those are
# machine-specific and unsafe to force — `biome doctor` / scripts/doctor.sh tells you if one is missing):
#   • GLFW — a FIRST-CLASS dependency here (unlike cheatah-gpu, where it is test-only): plotting owns a
#     window, so GLFW is the default windowing backend.
#   • the Vulkan loader + headers + validation layers (+ vulkan-tools for `vulkaninfo`) — the GPU
#     userspace cheatah-plot renders through (via cheatah-gpu's easy `gpu` layer).
#   • the Slang compiler `slangc` (bundled in the Vulkan SDK; we point you at it if absent).
#
#   scripts/install-deps.sh            # manual fallback: detect platform + install
#   scripts/install-deps.sh --dry-run  # print what it would do, install nothing
set -uo pipefail

DRY=0; [ "${1:-}" = "--dry-run" ] && DRY=1
run() { echo "+ $*"; [ "$DRY" = "1" ] || "$@"; }
have() { command -v "$1" >/dev/null 2>&1; }

echo "cheatah-plot: provisioning the plotting userspace stack (GLFW + GPU)…"

# --- macOS (Homebrew) -------------------------------------------------------------------------
if [ "$(uname -s)" = "Darwin" ]; then
    have brew || { echo "Homebrew not found — install it from https://brew.sh, then re-run."; exit 1; }
    # macOS renders through Apple's NATIVE Metal (cheatah-gpu's gpu.metal) — no MoltenVK / Vulkan needed.
    # Just the window backend (GLFW) and the Slang compiler (shader-slang = slangc, which targets Metal).
    run brew install glfw shader-slang
    echo "macOS: Metal is native (no MoltenVK); GLFW + Slang installed. Run 'biome doctor' to verify."
    exit 0
fi

# --- Windows (roadmap — not wired up yet) -----------------------------------------------------
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        cat <<'EOF'
Windows support is on the roadmap. For today, install manually:
  • GLFW (e.g. via vcpkg)
  • the LunarG Vulkan SDK (loader + slangc):  https://vulkan.lunarg.com/sdk/home
  • a GPU driver from your vendor (NVIDIA / AMD / Intel)
Then run scripts/doctor.sh. A winget/vcpkg one-shot will land later.
EOF
        exit 0 ;;
esac

# --- Linux (apt / dnf / pacman) ---------------------------------------------------------------
SUDO=""; [ "$(id -u)" = "0" ] || SUDO="sudo"

if have apt-get; then
    run $SUDO apt-get update
    run $SUDO apt-get install -y \
        libglfw3-dev \
        libvulkan-dev vulkan-validationlayers vulkan-tools \
        glslang-tools spirv-tools
elif have dnf; then
    run $SUDO dnf install -y \
        glfw-devel \
        vulkan-loader-devel vulkan-validation-layers vulkan-tools \
        glslang spirv-tools
elif have pacman; then
    run $SUDO pacman -S --needed --noconfirm \
        glfw \
        vulkan-icd-loader vulkan-headers vulkan-validation-layers vulkan-tools \
        glslang spirv-tools
else
    echo "No supported package manager (apt/dnf/pacman) found."
    echo "Install manually: GLFW dev, and the Vulkan loader+headers+validation layers."
    exit 1
fi

# --- Slang (slangc) — bundled in the Vulkan SDK; not in distro repos ---------------------------
if have slangc; then
    echo "slangc: found ($(command -v slangc))."
else
    cat <<'EOF'

slangc (the Slang shader compiler) was not found. It ships with the Vulkan SDK:
  • Install the LunarG Vulkan SDK:  https://vulkan.lunarg.com/sdk/home  (provides slangc)
  • or grab a Slang release:        https://github.com/shader-slang/slang/releases
Then re-run scripts/doctor.sh.
EOF
fi

echo
echo "Done. Verify with:  biome doctor   (or scripts/doctor.sh if you have no cheatah install yet)"
