#!/usr/bin/env bash
# gen-headers.sh — regenerate cheatah-plot's committed module headers from their `.purr` sources.
#
# cheatah-plot's submodules are authored in cheatah (`plot/<mod>/<mod>.purr`) and shipped as generated,
# header-only C++20 (`plot/<mod>/<mod>.hpp`, committed alongside the source — that is the biome-shipped
# artifact). This script is the one place that runs the transpile, so the committed header is never
# hand-edited and always matches its source:
#
#     purrc --emit-library --transparent --reexport cheatah::plot <mod>.purr -o <mod>.hpp
#
#   --emit-library   emit an importable library module (a signed header, + a .sha512 sidecar) in
#                    namespace cheatah::<mod>, rather than a runnable program.
#   --transparent    inline the generated C++ source into the committed header (no hidden archive), so
#                    users — and VS Code Go-to-Definition — always see the true C++. Matches cheatah's
#                    own first-party stdlib convention.
#   --reexport cheatah::plot   also surface the submodule under the host package namespace as
#                    `cheatah::plot::<mod>`, so the umbrella (`import plot`) aggregates the whole surface.
#
# purrc writes the `.hpp.sha512` sidecar itself (sha512sum format); the umbrella plot/plot.hpp is signed
# by scripts/sign-modules.sh (purrc never emits the hand-written umbrella).
#
#   scripts/gen-headers.sh           # regenerate every submodule header in place + re-sign the umbrella
#   scripts/gen-headers.sh --check   # verify the committed headers/sidecars are in sync (no writes); the
#                                    # QA gate runs this and fails on drift
#
# The toolchain (purrc) is the sibling ../cheatah checkout; override with CHEATAH_DIR.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
CHEATAH_DIR="${CHEATAH_DIR:-$(cd "$PWD/../cheatah" 2>/dev/null && pwd || true)}"

CHECK=0; [ "${1:-}" = "--check" ] && CHECK=1

find_tool() {
    local n="$1"
    for c in release debug asan; do
        [ -x "$CHEATAH_DIR/build/$c/bin/$n" ] && { echo "$CHEATAH_DIR/build/$c/bin/$n"; return 0; }
    done
    command -v "$n" 2>/dev/null
}
PURRC="$(find_tool purrc)"
[ -n "$PURRC" ] && [ -x "$PURRC" ] || {
    echo "gen-headers: no purrc toolchain (set CHEATAH_DIR or place cheatah at ../cheatah)"; exit 2; }

REEXPORT="cheatah::plot"
emit() { "$PURRC" --emit-library --transparent --reexport "$REEXPORT" "$1" -o "$2"; }

shopt -s nullglob
mods=(plot/*/*.purr)
[ ${#mods[@]} -gt 0 ] || { echo "gen-headers: no plot/<mod>/<mod>.purr sources found"; exit 1; }

status=0

if [ "$CHECK" = "1" ]; then
    # Regenerate to a throwaway dir and diff against the committed artifacts — robust whether or not the
    # tree is committed yet. Any difference means a header/sidecar drifted from its `.purr`.
    TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
    for src in "${mods[@]}"; do
        mod="$(basename "$src" .purr)"; dir="$(dirname "$src")"
        if ! emit "$src" "$TMP/$mod.hpp" 2>"$TMP/$mod.err"; then
            echo "gen-headers: purrc failed on $src:"; sed 's/^/    /' "$TMP/$mod.err"; status=1; continue
        fi
        for ext in hpp hpp.sha512; do
            if ! diff -q "$dir/$mod.$ext" "$TMP/$mod.$ext" >/dev/null 2>&1; then
                echo "gen-headers: DRIFT — $dir/$mod.$ext is out of sync with $src (run scripts/gen-headers.sh, commit it)"
                status=1
            fi
        done
    done
    [ "$status" -eq 0 ] && echo "gen-headers: all generated headers + sidecars in sync with their .purr."
    exit "$status"
fi

# Write mode: regenerate each submodule header (+ its sidecar) in place, then re-sign the umbrella.
for src in "${mods[@]}"; do
    mod="$(basename "$src" .purr)"; out="$(dirname "$src")/$mod.hpp"
    if emit "$src" "$out"; then echo "generated: $out (+ .sha512)"; else echo "gen-headers: purrc failed on $src"; status=1; fi
done
bash scripts/sign-modules.sh || status=1
exit "$status"
