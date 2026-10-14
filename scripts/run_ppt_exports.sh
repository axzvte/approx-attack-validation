#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

OUT_DIR="$ROOT_DIR/ppt_results"
mkdir -p "$OUT_DIR"

cmake -S . -B build
cmake --build build --target ppt_stage3_export -j"$(nproc)"

for app in conv sharpen dct sobel; do
    echo
    echo "============================================================"
    echo "PPT export: $app"
    echo "============================================================"

    time ./build/ppt_stage3_export \
        "$app" data "$OUT_DIR/$app" 4 10 0 24 24 \
        | tee "$OUT_DIR/${app}_ppt_export.log"
done

echo
echo "============================================================"
echo "All PPT exports finished"
echo "Results: $OUT_DIR"
echo "============================================================"
