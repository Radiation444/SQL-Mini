#!/usr/bin/env bash
# bench.sh — build and run the TinySQL benchmark suite
# Usage:  ./bench.sh [--clean]
#
# Run this from the MSYS2 UCRT64 terminal (orange), not MINGW64 (blue).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# ── pick the right g++ ────────────────────────────────────────────────────────
# On MSYS2 the UCRT64 and MINGW64 toolchains live in different prefixes.
# Prefer the UCRT64 compiler; fall back to whatever is on PATH.
if [[ -x "/c/msys64/ucrt64/bin/g++" ]]; then
    CXX="/c/msys64/ucrt64/bin/g++"
elif [[ -x "/ucrt64/bin/g++" ]]; then
    CXX="/ucrt64/bin/g++"
else
    CXX="${CXX:-g++}"
fi

BINARY="bench.exe"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra"
SOURCES="bench_main.cpp btree.cpp buffer_pool.cpp node.cpp pager.cpp \
         schema.cpp slotted_page.cpp table.cpp sql_engine.cpp"

# ── optional --clean ──────────────────────────────────────────────────────────
if [[ "${1:-}" == "--clean" ]]; then
    echo "[bench] Cleaning..."
    rm -f "$BINARY" *.o bench_raw.db bench_sql.db
    echo "[bench] Done."
    exit 0
fi

# ── build ─────────────────────────────────────────────────────────────────────
echo "[bench] Compiler : $CXX"
echo "[bench] Flags    : $CXXFLAGS"
echo "[bench] Compiling..."
$CXX $CXXFLAGS -o "$BINARY" $SOURCES
echo "[bench] Build OK -> ./$BINARY"
echo ""

# ── run ───────────────────────────────────────────────────────────────────────
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
LOG="bench_${TIMESTAMP}.txt"

echo "[bench] Running (may take 30-60s)..."
echo ""

if command -v tee >/dev/null 2>&1; then
    ./"$BINARY" 2>&1 | tee "$LOG"
else
    ./"$BINARY" 2>&1 > "$LOG"
    cat "$LOG"
fi

echo ""
echo "[bench] Results saved -> $LOG"

# ── diff against previous run ─────────────────────────────────────────────────
PREV=$(ls bench_*.txt 2>/dev/null | grep -v "$LOG" | sort | tail -1 || true)
if [[ -n "$PREV" ]]; then
    echo ""
    echo "[bench] Diff vs previous run ($PREV):"
    echo "        (< = before, > = after)"
    echo ""
    grep -E '^\s{2}[A-Za-z].*\|' "$PREV" > _prev.txt 2>/dev/null || true
    grep -E '^\s{2}[A-Za-z].*\|' "$LOG"  > _curr.txt 2>/dev/null || true
    if [[ -s _prev.txt && -s _curr.txt ]]; then
        diff _prev.txt _curr.txt || true
    else
        echo "        (nothing to diff)"
    fi
    rm -f _prev.txt _curr.txt
fi