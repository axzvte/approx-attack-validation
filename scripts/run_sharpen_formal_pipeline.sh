#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

STAMP="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="$ROOT_DIR/sharpen_formal_$STAMP"

mkdir -p "$OUT_DIR"

echo "============================================================"
echo "Sharpening formal three-stage pipeline"
echo "Objective:"
echo "  Hard constraints: Global >= 30 dB and Advantage > 0"
echo "  Search priority : lower ROI PSNR"
echo "  Tie-break       : higher Redistribution Advantage"
echo "Stage2 H* ranking:"
echo "  Success -> lower MaxROI -> lower MeanROI -> higher MeanAdv -> fewer nodes"
echo "ROI: current data/mask/roi_mask.jpg"
echo "Output: $OUT_DIR"
echo "Started: $(date)"
echo "============================================================"

cmake -S . -B build

cmake --build build --target \
    sharpen_multi_state_joint_search \
    hardware_config_statistics \
    automatic_fixed_hardware_pipeline \
    -j"$(nproc)"

echo
echo "============================================================"
echo "[1/3] Sharpen Stage1: 10-image free search"
echo "============================================================"

rm -f "$OUT_DIR"/sharpen_joint_search_best_image_*.csv

for i in $(seq 1 10); do
    printf -v idx "%02d" "$i"

    echo
    echo "========== Sharpen Stage1 image_$idx =========="

    time ./build/sharpen_multi_state_joint_search \
        data "$i" "$OUT_DIR" 4 10 0 24 24 \
        | tee "$OUT_DIR/sharpen_stage1_image_$idx.log"
done

echo
echo "============================================================"
echo "[2/3] Stage1 hardware statistics"
echo "============================================================"

./build/hardware_config_statistics "$OUT_DIR" sharpen 10 \
    | tee "$OUT_DIR/sharpen_hardware_statistics.log"

echo
echo "============================================================"
echo "[3/3] Stage2 robust H* selection + Stage3 final validation"
echo "============================================================"

time ./build/automatic_fixed_hardware_pipeline \
    sharpen data "$OUT_DIR" 4 10 0 24 24 \
    | tee "$OUT_DIR/sharpen_stage2_stage3.log"

echo
echo "============================================================"
echo "Sharpening formal pipeline finished"
echo "Finished: $(date)"
echo "Results: $OUT_DIR"
echo

echo "Stage2 hardware screening:"
grep -A 16 "sharpen Stage2 automatic hardware screening summary" \
    "$OUT_DIR/sharpen_stage2_stage3.log" || true

echo
echo "Stage3 final validation:"
grep -A 10 "sharpen Stage3 final validation summary" \
    "$OUT_DIR/sharpen_stage2_stage3.log" || true

echo "============================================================"
