#!/usr/bin/env bash
# Run one fixed mixed-adder input profile for each of the four applications.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"
ROI_RUN="${ROI_RUN:-$HOME/projects/u2net_roi/output/union_rc_850_01}"
IMAGE_DIR="${IMAGE_DIR:-$HOME/datasets/PASCAL-S/datasets/imgs/pascal}"
IMAGE_COUNT="${IMAGE_COUNT:-850}"
RUN_DIR="${RUN_DIR:-$PROJECT_DIR/circuit_processing/results/all_apps_$(date +%Y%m%d_%H%M%S)}"
PROFILES="$PROJECT_DIR/circuit_processing/configs/circuits"
PIPELINE="$PROJECT_DIR/circuit_processing/scripts/fixed_roi_pipeline.py"

printf 'Results directory: %s\n' "$RUN_DIR"

"$PYTHON_BIN" "$PIPELINE" freeze \
  --circuit "$PROFILES/sharpen_mixed.json" \
  --circuit "$PROFILES/conv_mixed.json" \
  --circuit "$PROFILES/sobel_mixed.json" \
  --circuit "$PROFILES/dct_mixed.json" \
  --roi-run "$ROI_RUN" --threshold 85 \
  --output "$RUN_DIR/frozen.json"

"$PYTHON_BIN" "$PIPELINE" verify \
  --config "$RUN_DIR/frozen.json" \
  --images "$IMAGE_DIR" --count "$IMAGE_COUNT" \
  --output "$RUN_DIR/verify"
