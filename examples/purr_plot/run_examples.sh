#!/usr/bin/env bash
# run_examples.sh — compile + run every plotting example, writing PNGs into examples/purr_plot/out/.
# Skips cleanly when the cheatah toolchain isn't present.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
CHEATAH_DIR="${CHEATAH_DIR:-$(cd "$PWD/../cheatah" 2>/dev/null && pwd || true)}"
find_tool() { local n="$1"; for c in release debug asan; do
    [ -x "$CHEATAH_DIR/build/$c/bin/$n" ] && { echo "$CHEATAH_DIR/build/$c/bin/$n"; return 0; }; done; }
PURRC="$(find_tool purrc)"; CHEATAH="$(find_tool cheatah)"
[ -n "$PURRC" ] && [ -n "$CHEATAH" ] || { echo "[examples] no cheatah toolchain — skipping."; exit 0; }

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
mkdir -p examples/purr_plot/out
fails=0
for ex in examples/purr_plot/[0-9]*.purr; do
    name="$(basename "$ex" .purr)"
    printf '── %s ──\n' "$name"
    if ! "$PURRC" --import-root "$PWD" "$ex" -o "$W/$name.so" 2>"$W/$name.err"; then
        echo "  COMPILE FAILED"; sed 's/^/    /' "$W/$name.err" | head -8; fails=$((fails + 1)); continue
    fi
    ( cd examples/purr_plot && "$CHEATAH" "$W/$name.so" ) || { echo "  RUN FAILED"; fails=$((fails + 1)); }
done
ls examples/purr_plot/out/*.png >/dev/null 2>&1 || { echo "[examples] no PNGs produced"; exit 1; }
[ "$fails" -eq 0 ] || { echo "[examples] $fails example(s) failed"; exit 1; }
echo "[examples] all examples rendered — see examples/purr_plot/out/"
