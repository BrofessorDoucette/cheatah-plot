#!/usr/bin/env bash
# Measure cheatah-plot test coverage with clang source-based coverage — the in-process unit tests
# (cheatah_plot_tests) that exercise the generated plot/ headers.
#
#   scripts/coverage.sh               # per-file summary report for plot/
#   scripts/coverage.sh show <file>   # uncovered lines of one file, e.g. plot/scale/scale.hpp
#   scripts/coverage.sh funcs <file>  # per-function coverage of one file
#   scripts/coverage.sh update-readme # rewrite the coverage table in README.md
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

B=build/cov
# clang source-based coverage (-fprofile-instr-generate -fcoverage-mapping is clang syntax), so pin
# clang for this build tree regardless of the machine's default compiler. Override via CLANGXX/CLANG.
CLANGXX="${CLANGXX:-clang++}"; CLANG="${CLANG:-clang}"
command -v "$CLANGXX" >/dev/null 2>&1 || { echo "coverage: $CLANGXX not found — clang is required for source-based coverage"; exit 1; }
cmake -S . -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCHEATAH_PLOT_BUILD_TESTS=ON \
  -DCMAKE_C_COMPILER="$CLANG" -DCMAKE_CXX_COMPILER="$CLANGXX" \
  -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
  -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate -fcoverage-mapping" >/tmp/cheatah_plot_cov_cfg.log 2>&1 \
  || { tail -20 /tmp/cheatah_plot_cov_cfg.log; exit 1; }
cmake --build "$B" --target cheatah_plot_tests >/tmp/cheatah_plot_cov_build.log 2>&1 \
  || { tail -25 /tmp/cheatah_plot_cov_build.log; exit 1; }

( cd "$B"
  LLVM_PROFILE_FILE=t1.profraw ./bin/cheatah_plot_tests >/dev/null 2>&1
  llvm-profdata merge -sparse t1.profraw -o merged.profdata )

OBJS=(./"$B"/bin/cheatah_plot_tests)
PROF="-instr-profile=$B/merged.profdata"
# The generated, host-testable plot surface. git's '*' spans '/', so this matches plot/plot.hpp and the
# submodule headers alike. `--cached --others --exclude-standard` also lists not-yet-committed headers
# (this repo is authored ahead of its first commit) while still honouring .gitignore.
SRCS=$(git ls-files --cached --others --exclude-standard 'plot/*.hpp' | grep -v '/tests/')

case "${1:-report}" in
    show)  llvm-cov show   "${OBJS[@]}" $PROF "${2:?usage: coverage.sh show <file>}" 2>/dev/null \
             | grep -nE '\|[[:space:]]*0\|' || echo "all lines covered in ${2}" ;;
    funcs) llvm-cov report "${OBJS[@]}" $PROF -show-functions "${2:?usage: coverage.sh funcs <file>}" 2>/dev/null ;;
    update-readme)
        # Functions / regions / branches come from llvm-cov's per-file report TOTAL.
        # Columns: Regions MissedRegions RCover Functions MissedFuncs FExec Lines
        # MissedLines LCover Branches MissedBranches BCover.
        total=$(llvm-cov report "${OBJS[@]}" $PROF $SRCS 2>/dev/null | awk '$1=="TOTAL"{$1="";print}')
        read -r regions mreg rcov funcs mfun fexec lines mlin lcov branches mbr bcov <<<"$total"
        # LINE coverage is computed from the MERGED execution view (llvm-cov export segments) rather than
        # the report summary, counting each REGION-ENTRY line once. plot's shipped headers are TEMPLATES
        # (purrc emits `auto` / concept-constrained functions), and the report summary expands a source
        # line per-instantiation and miscounts it "missed" when one instantiation of an edge path isn't
        # hit — even though the line IS executed. Merging region-entry counts across instantiations fixes
        # that (and ignores structural lines like a `}` that has no region entry). A genuinely-untested
        # statement still fails: its region entry has count 0.
        read -r lcn lt lcov < <(llvm-cov export "${OBJS[@]}" $PROF $SRCS 2>/dev/null | python3 -c '
import json, sys
from collections import defaultdict
d = json.load(sys.stdin)
cov = tot = 0
for f in d["data"][0]["files"]:
    name = f["filename"]
    if "/plot/" not in name or "/tests/" in name:
        continue
    mx = defaultdict(int); seen = set()
    for s in f["segments"]:
        line, count, hasCount, isEntry = s[0], s[2], s[3], s[4]
        if hasCount and isEntry:           # region-entry lines, merged across instantiations
            seen.add(line); mx[line] = max(mx[line], count)
    tot += len(seen); cov += sum(1 for l in seen if mx[l] > 0)
pct = ("%.2f%%" % (100.0 * cov / tot)) if tot else "100.00%"
print(cov, tot, pct)
')
        python3 - "$lcov" "$lcn" "$lt" "$fexec" "$((funcs - mfun))" "$funcs" "$rcov" "$bcov" <<'PY'
import re, sys
lcov, lcn, lt, fexec, fcn, ft, rcov, bcov = sys.argv[1:9]
table = (
    "<!-- coverage:start -->\n"
    "| Metric | plot package |\n"
    "|--------|--------------|\n"
    f"| **Lines** | {lcov} ({lcn}/{lt}) |\n"
    f"| **Functions** | {fexec} ({fcn}/{ft}) |\n"
    f"| Regions | {rcov} |\n"
    f"| Branches | {bcov} |\n"
    "<!-- coverage:end -->"
)
src = open("README.md").read()
out, n = re.subn(r"<!-- coverage:start -->.*?<!-- coverage:end -->", lambda _: table, src, flags=re.S)
if n != 1:
    sys.stderr.write("coverage markers not found in README.md\n"); sys.exit(1)
open("README.md", "w").write(out)
print(f"README coverage table: lines {lcov} ({lcn}/{lt}), functions {fexec} ({fcn}/{ft})")
PY
        ;;
    *)     llvm-cov report "${OBJS[@]}" $PROF $SRCS 2>/dev/null ;;
esac
