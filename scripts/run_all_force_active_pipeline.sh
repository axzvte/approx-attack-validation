#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

STAMP="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="$ROOT_DIR/force_active_pipeline_$STAMP"

CONV_DIR="$OUT_DIR/conv"
SHARPEN_DIR="$OUT_DIR/sharpen"
DCT_DIR="$OUT_DIR/dct"
SOBEL_DIR="$OUT_DIR/sobel"

mkdir -p "$CONV_DIR" "$SHARPEN_DIR" "$DCT_DIR" "$SOBEL_DIR"

echo "============================================================"
echo "Force-active full pipeline"
echo "Hard constraints: Global >= 30 dB; Advantage > 0"
echo "Search objective : minimize ROI PSNR"
echo "Tie-break        : maximize Redistribution Advantage"
echo "Fixed hardware   : every H* node MUST receive an interval"
echo "                   (no enable pin / no skipped fixed node)"
echo "ROI              : current data/mask/roi_mask.jpg"
echo "Output directory : $OUT_DIR"
echo "Started          : $(date)"
echo "============================================================"

cmake -S . -B build

cmake --build build --target     test_dct_multi_state_joint_search     conv_node_sensitivity_analysis     dct_node_sensitivity_analysis     sobel_node_sensitivity_analysis     conv_multi_state_joint_search     sharpen_multi_state_joint_search     dct_multi_state_joint_search     sobel_multi_state_joint_search     hardware_config_statistics     automatic_fixed_hardware_pipeline     -j"$(nproc)"

echo
echo "============================================================"
echo "[0/9] Search regression test"
echo "============================================================"

./build/test_dct_multi_state_joint_search

echo
echo "============================================================"
echo "[1/9] Refresh sensitivity under current ROI"
echo "============================================================"

time ./build/conv_node_sensitivity_analysis data "$CONV_DIR"     | tee "$CONV_DIR/conv_node_sensitivity.log"

time ./build/dct_node_sensitivity_analysis data "$DCT_DIR"     | tee "$DCT_DIR/dct_node_sensitivity.log"

time ./build/sobel_node_sensitivity_analysis data "$SOBEL_DIR"     | tee "$SOBEL_DIR/sobel_node_sensitivity.log"

echo
echo "============================================================"
echo "[2/9] Conv Stage1"
echo "============================================================"

for i in $(seq 1 10); do
    printf -v idx "%02d" "$i"
    echo "========== Conv Stage1 image_$idx =========="
    time ./build/conv_multi_state_joint_search         data "$i" 5 "$CONV_DIR" 4 10 0 24 24         | tee "$CONV_DIR/conv_stage1_image_$idx.log"
done

./build/hardware_config_statistics "$CONV_DIR" conv 10     | tee "$CONV_DIR/conv_hardware_statistics.log"

echo
echo "============================================================"
echo "[3/9] Conv Stage2 + Stage3 (all H* nodes active)"
echo "============================================================"

time ./build/automatic_fixed_hardware_pipeline     conv data "$CONV_DIR" 4 10 0 24 24     | tee "$CONV_DIR/conv_stage2_stage3.log"

echo
echo "============================================================"
echo "[4/9] Sharpening Stage1"
echo "============================================================"

for i in $(seq 1 10); do
    printf -v idx "%02d" "$i"
    echo "========== Sharpen Stage1 image_$idx =========="
    time ./build/sharpen_multi_state_joint_search         data "$i" "$SHARPEN_DIR" 4 10 0 24 24         | tee "$SHARPEN_DIR/sharpen_stage1_image_$idx.log"
done

./build/hardware_config_statistics "$SHARPEN_DIR" sharpen 10     | tee "$SHARPEN_DIR/sharpen_hardware_statistics.log"

echo
echo "============================================================"
echo "[5/9] Sharpening Stage2 + Stage3 (all H* nodes active)"
echo "============================================================"

time ./build/automatic_fixed_hardware_pipeline     sharpen data "$SHARPEN_DIR" 4 10 0 24 24     | tee "$SHARPEN_DIR/sharpen_stage2_stage3.log"

echo
echo "============================================================"
echo "[6/9] DCT Stage1"
echo "============================================================"

for i in $(seq 1 10); do
    printf -v idx "%02d" "$i"
    echo "========== DCT Stage1 image_$idx =========="
    time ./build/dct_multi_state_joint_search         data "$i" 5 "$DCT_DIR" 4 10 0 24 24         | tee "$DCT_DIR/dct_stage1_image_$idx.log"
done

./build/hardware_config_statistics "$DCT_DIR" dct 10     | tee "$DCT_DIR/dct_hardware_statistics.log"

echo
echo "============================================================"
echo "[7/9] DCT Stage2 + Stage3 (all H* nodes active)"
echo "============================================================"

time ./build/automatic_fixed_hardware_pipeline     dct data "$DCT_DIR" 4 10 0 24 24     | tee "$DCT_DIR/dct_stage2_stage3.log"

echo
echo "============================================================"
echo "[8/9] Sobel Stage1"
echo "============================================================"

for i in $(seq 1 10); do
    printf -v idx "%02d" "$i"
    echo "========== Sobel Stage1 image_$idx =========="
    time ./build/sobel_multi_state_joint_search         data "$i" 5 "$SOBEL_DIR" 4 10 0 24 24         | tee "$SOBEL_DIR/sobel_stage1_image_$idx.log"
done

./build/hardware_config_statistics "$SOBEL_DIR" sobel 10     | tee "$SOBEL_DIR/sobel_hardware_statistics.log"

echo
echo "============================================================"
echo "[9/9] Sobel Stage2 + Stage3 (all H* nodes active)"
echo "============================================================"

time ./build/automatic_fixed_hardware_pipeline     sobel data "$SOBEL_DIR" 4 10 0 24 24     | tee "$SOBEL_DIR/sobel_stage2_stage3.log"

echo
echo "============================================================"
echo "Force-active full pipeline finished"
echo "Finished: $(date)"
echo "Results: $OUT_DIR"
echo

for app in conv sharpen dct sobel; do
    echo "-------------------- $app Stage2 --------------------"
    LOG="$OUT_DIR/$app/${app}_stage2_stage3.log"
    grep -A 18 "$app Stage2 automatic hardware screening summary" "$LOG" || true
    echo
    echo "-------------------- $app Stage3 --------------------"
    grep -A 10 "$app Stage3 final validation summary" "$LOG" || true
    echo
done

echo "============================================================"
