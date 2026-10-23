#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

# H* labels selected using previous Stage2 data, never Stage3 data.
if [[ "$#" -gt 0 ]]; then
    SOURCE_DIR="$1"
else
    SEARCH_DIRS=("$ROOT_DIR")
    if [[ -d "$ROOT_DIR/archive/experiments" ]]; then
        SEARCH_DIRS+=("$ROOT_DIR/archive/experiments")
    fi
    SOURCE_DIR="$(
        find "${SEARCH_DIRS[@]}" -maxdepth 1 -type d \
            -name 'force_active_pipeline_*' -printf '%T@ %p\n' \
            | sort -nr | head -n 1 | cut -d' ' -f2-
    )"
fi

if [[ -z "$SOURCE_DIR" || ! -d "$SOURCE_DIR" ]]; then
    echo "Cannot find previous force_active_pipeline_* directory." >&2
    echo "Usage: bash scripts/rerank_force_active_stage3.sh [old_result_dir]" >&2
    exit 1
fi

SOURCE_DIR="$(realpath "$SOURCE_DIR")"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="$ROOT_DIR/force_active_reranked_$STAMP"
mkdir -p "$OUT_DIR"

echo "========================================"
echo "Stage3 re-evaluation with reranked H*"
echo "Previous Stage1 directory: $SOURCE_DIR"
echo "New results: $OUT_DIR"
echo "Ranking: Success -> MeanROI -> MeanAdv -> Nodes -> MaxROI"
echo "Constraints: Global >= 30dB and Advantage > 0"
echo "             Every fixed H* node has an interval"
echo "========================================"

cmake -S . -B build
cmake --build build --target automatic_fixed_hardware_pipeline -j"$(nproc)"

for app in conv sharpen dct sobel; do
    case "$app" in
        conv) hw="A" ;;
        sharpen) hw="B" ;;
        dct) hw="A" ;;
        sobel) hw="B" ;;
    esac

    src="$SOURCE_DIR/$app"
    dest="$OUT_DIR/$app"
    mkdir -p "$dest"

    echo "----------------------------------------"
    echo "$app, selected H* = $hw"
    echo "----------------------------------------"

    for i in $(seq -w 1 10); do
        file="$src/$app"_joint_search_best_image_"$i".csv
        if [[ ! -f "$file" ]]; then
            echo "Missing Stage1 result: $file" >&2
            exit 1
        fi
        cp "$file" "$dest/"
    done

    time ./build/automatic_fixed_hardware_pipeline \
        "$app" data "$dest" 4 10 0 24 24 --stage3-only "$hw" \
        | tee "$dest/$app"_stage3_reranked.log
done

echo
echo "=============== Stage3 summaries ==============="
for app in conv sharpen dct sobel; do
    grep -A 10 "$app Stage3 final validation summary" \
        "$OUT_DIR/$app/$app"_stage3_reranked.log
    echo
done

echo "Detailed metrics in $OUT_DIR/<app>/stage3_detail_<H*>.csv"
echo "Final images in $OUT_DIR/<app>/stage3_final_<H*>/"
echo "Finished. Results: $OUT_DIR"
