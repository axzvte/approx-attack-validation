#!/usr/bin/env bash
set -euo pipefail
EXE="$1"
DATA="$2"
TRIAL="$(mktemp -d "$(dirname "$EXE")/fixed-cli-test-XXXXXX")"
"$EXE" --fixed "$DATA" "$TRIAL" 1 input2 8 0 21 21 >"$TRIAL/console.log" 2>&1
awk -F, 'NR==2 {if ($3!=1 || $5!="input2" || $6!=8 || $2!=21) exit 1} END {if(NR!=2) exit 1}' "$TRIAL/summary.csv"
test -s "$TRIAL/stage3_image_21_attack.png"
echo "Fixed hardware CLI preserves supplied structure and saves the validated output"
