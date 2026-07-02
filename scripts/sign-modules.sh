#!/usr/bin/env bash
# sign-modules.sh — write the .sha512 sidecars that mark cheatah-plot's module headers as VERIFIED
# cheatah modules. This is what makes the extension biome-installable: with the sidecar present, purrc
# resolves `import plot` / `import plot.*` on the extension path (CHEATAH_MODULE_PATH, which
# `biome add cheatah-plot` sets) and the runtime verifies each header against its checksum on load — so
# a user with a standard cheatah install never touches git or --import-root.
#
# The sidecar is sha512sum format ("<hex>  <basename>\n"): identical to what `purrc --emit-library`
# emits for the generated submodule headers (compiler/purrc.cpp writes hex + "  " + base_name), and
# what `sha512sum -c` validates. The runtime verifier reads only the first whitespace token, so the
# filename suffix is cosmetic — but keeping it matches purrc's own output and stays `-c`-checkable.
#
# The hand-written umbrella plot/plot.hpp is signed HERE (purrc never emits it). The generated submodule
# headers (plot/<mod>/<mod>.hpp) are already signed by purrc during scripts/gen-headers.sh; re-signing
# them here reproduces the exact same bytes, so it is safe and keeps every module header covered. Re-run
# whenever a header changes; the QA gate checks the sidecars are in sync.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

shopt -s nullglob
for h in plot/plot.hpp plot/*/*.hpp; do
    [ -f "$h" ] || continue
    # Sign from the header's own directory so the sidecar records just the basename (matching purrc).
    ( cd "$(dirname "$h")" && sha512sum "$(basename "$h")" > "$(basename "$h").sha512" )
    echo "signed: $h.sha512"
done
