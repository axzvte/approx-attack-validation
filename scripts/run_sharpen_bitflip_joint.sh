#!/usr/bin/env bash
# User-invoked only. Authoring agents must not compile or run this experiment.
set -euo pipefail
if [[ $# -lt 2 || $# -gt 9 ]]; then
    echo "Usage: bash scripts/run_sharpen_bitflip_joint.sh DATA_DIR NEW_OUTPUT_DIR [image=1] [beam=4] [refinement=1] [boundary_budget=24] [top_per_count=2] [restarts=2] [global_psnr=30]" >&2
    exit 2
fi
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
DATA="$1"
OUT="$2"
shift 2
[[ -f "$DATA/mask/roi_mask.jpg" ]] || { echo "Missing ROI in $DATA" >&2; exit 2; }
mkdir -p -- "$(dirname -- "$OUT")"
mkdir -- "$OUT"
printf '%s\n' 'completed=0' > "$OUT/RUN.txt"
git rev-parse HEAD > "$OUT/code_revision.txt"
cmake -S . -B build-bitflip -DCMAKE_BUILD_TYPE=Release 2>&1 | tee "$OUT/build.log"
cmake --build build-bitflip --target sharpen_bitflip_joint -j4 2>&1 | tee -a "$OUT/build.log"
./build-bitflip/sharpen_bitflip_joint "$DATA" "$OUT/report" "$@" 2>&1 | tee "$OUT/run.log"
printf '%s\n' 'completed=1' >> "$OUT/RUN.txt"
echo "Numeric results: $OUT/report/summary.csv"
echo "Node configurations: $OUT/report/configurations.csv"
echo "Node ablations: $OUT/report/ablations.csv"
echo "Visual review required: $OUT/report/*comparison.png"
