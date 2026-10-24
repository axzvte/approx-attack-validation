#!/usr/bin/env bash
# The user invokes this script in their own terminal; authoring agents must not run it.
set -euo pipefail
if [[ $# -lt 2 || $# -gt 5 ]]; then
    echo "Usage: bash scripts/run_bitflip_interval_trial.sh DATA_DIR NEW_OUTPUT_DIR [first=1] [last=first] [boundary_budget=64]" >&2
    exit 2
fi
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
DATA="$1"
OUT="$2"
FIRST="${3:-1}"
LAST="${4:-$FIRST}"
BUDGET="${5:-64}"
[[ "$FIRST" =~ ^[0-9]+$ && "$LAST" =~ ^[0-9]+$ && "$BUDGET" =~ ^[0-9]+$ ]] || {
    echo "Image indices and budget must be nonnegative integers" >&2; exit 2;
}
# Force decimal parsing, including indices supplied with leading zeroes.
FIRST=$((10#$FIRST)); LAST=$((10#$LAST)); BUDGET=$((10#$BUDGET))
if (( FIRST < 1 || LAST > 30 || FIRST > LAST || BUDGET == 1 )); then
    echo "Use images 1..30 and budget 0 or >=2" >&2; exit 2
fi
[[ -f "$DATA/mask/roi_mask.jpg" ]] || { echo "Missing ROI in $DATA" >&2; exit 2; }
mkdir -p -- "$(dirname -- "$OUT")"
mkdir -- "$OUT"  # Exclusive ownership; never reuse an old run directory.
printf '%s\n' 'completed=0' > "$OUT/RUN.txt"
cmake -S . -B build-bitflip -DCMAKE_BUILD_TYPE=Release 2>&1 | tee "$OUT/build.log"
cmake --build build-bitflip --target sharpen_bitflip_diagnostics -j4 2>&1 | tee -a "$OUT/build.log"
./build-bitflip/sharpen_bitflip_diagnostics "$DATA" "$OUT/profiles" "$FIRST" "$LAST" \
    2>&1 | tee "$OUT/profile.log"
python3 scripts/search_bitflip_intervals.py "$OUT/profiles" "$OUT/intervals" \
    --boundary-budget "$BUDGET" --global-psnr 30 2>&1 | tee "$OUT/interval.log"
printf '%s\n' 'completed=1' >> "$OUT/RUN.txt"
echo "Share these files: $OUT/intervals/selected_intervals.csv and $OUT/intervals/comparisons.csv"
