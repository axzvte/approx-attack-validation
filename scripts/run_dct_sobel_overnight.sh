#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

STAMP="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="$ROOT_DIR/overnight_results_$STAMP"

DCT_DIR="$OUT_DIR/dct"
SOBEL_DIR="$OUT_DIR/sobel"

mkdir -p "$DCT_DIR" "$SOBEL_DIR"

echo "============================================================"
echo "DCT + Sobel overnight pipeline"
echo "Output directory: $OUT_DIR"
echo "Started: $(date)"
echo "============================================================"

cmake -S . -B build

cmake --build build --target \
    dct_node_sensitivity_analysis \
    sobel_node_sensitivity_analysis \
    dct_multi_state_joint_search \
    sobel_multi_state_joint_search \
    hardware_config_statistics \
    automatic_fixed_hardware_pipeline \
    -j"$(nproc)"

echo
echo "============================================================"
echo "[1/6] Refresh node sensitivity"
echo "============================================================"

time ./build/dct_node_sensitivity_analysis data . \
    | tee "$DCT_DIR/dct_node_sensitivity.log"

time ./build/sobel_node_sensitivity_analysis data . \
    | tee "$SOBEL_DIR/sobel_node_sensitivity.log"

cp dct_node_sensitivity_summary.csv "$DCT_DIR/dct_node_sensitivity_summary.csv"
cp sobel_node_sensitivity_summary.csv "$SOBEL_DIR/sobel_node_sensitivity_summary.csv"

echo
echo "============================================================"
echo "[2/6] DCT Stage1: 10-image free search"
echo "============================================================"

rm -f "$DCT_DIR"/dct_joint_search_best_image_*.csv

for i in $(seq 1 10); do
    printf -v idx "%02d" "$i"

    echo
    echo "========== DCT Stage1 image_$idx =========="

    time ./build/dct_multi_state_joint_search \
        data "$i" 5 "$DCT_DIR" 4 10 0 24 24 \
        | tee "$DCT_DIR/dct_stage1_image_$idx.log"
done

echo
echo "============================================================"
echo "[3/6] DCT statistics + automatic Stage2/Stage3"
echo "============================================================"

./build/hardware_config_statistics "$DCT_DIR" dct 10 \
    | tee "$DCT_DIR/dct_hardware_statistics.log"

time ./build/automatic_fixed_hardware_pipeline \
    dct data "$DCT_DIR" 4 10 0 24 24 \
    | tee "$DCT_DIR/dct_stage2_stage3_pipeline.log"

echo
echo "============================================================"
echo "[4/6] Sobel Stage1: 10-image free search"
echo "============================================================"

rm -f "$SOBEL_DIR"/sobel_joint_search_best_image_*.csv

for i in $(seq 1 10); do
    printf -v idx "%02d" "$i"

    echo
    echo "========== Sobel Stage1 image_$idx =========="

    time ./build/sobel_multi_state_joint_search \
        data "$i" 5 "$SOBEL_DIR" 4 10 0 24 24 \
        | tee "$SOBEL_DIR/sobel_stage1_image_$idx.log"
done

echo
echo "============================================================"
echo "[5/6] Sobel statistics + automatic Stage2/Stage3"
echo "============================================================"

./build/hardware_config_statistics "$SOBEL_DIR" sobel 10 \
    | tee "$SOBEL_DIR/sobel_hardware_statistics.log"

time ./build/automatic_fixed_hardware_pipeline \
    sobel data "$SOBEL_DIR" 4 10 0 24 24 \
    | tee "$SOBEL_DIR/sobel_stage2_stage3_pipeline.log"

echo
echo "============================================================"
echo "[6/6] Overnight pipeline finished"
echo "Finished: $(date)"
echo "Results: $OUT_DIR"
echo
echo "DCT final summary:"
grep -A 10 "dct Stage3 final validation summary" \
    "$DCT_DIR/dct_stage2_stage3_pipeline.log" || true

echo
echo "Sobel final summary:"
grep -A 10 "sobel Stage3 final validation summary" \
    "$SOBEL_DIR/sobel_stage2_stage3_pipeline.log" || true

echo "============================================================"
