#!/usr/bin/env bash
set -euo pipefail

scripts_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd -- "$scripts_dir/../../.." && pwd)
case "${1:-sharpen}" in
    all) apps=(sharpen conv sobel dct) ;;
    sharpen|conv|sobel|dct) apps=("${1:-sharpen}") ;;
    *) echo "Usage: bash $0 [sharpen|conv|sobel|dct|all]" >&2; exit 2 ;;
esac

export DC_LIB="${DC_LIB:-$HOME/techlib/nangate45/stdcells.db}"
export DC_PERIOD="${DC_PERIOD:-10}"
dc_shell="${DC_SHELL:-$HOME/Synopsys/dc2016/bin/dc_shell}"
compat_lib="${DC_COMPAT_LIB:-$HOME/synopsys-compat/lib}"
test -r "$DC_LIB" || { echo "Missing library: $DC_LIB" >&2; exit 1; }
test -r "$dc_shell" || { echo "Missing DC launcher: $dc_shell" >&2; exit 1; }
test -r "$scripts_dir/../area/manifest.json" || {
    echo "Generate the area RTL with generate_rtl.py --area-scope first." >&2
    exit 1
}
export DC_LIB="$(realpath -- "$DC_LIB")"
export DC_RTL="$(realpath -- "$scripts_dir/../area/rtl")"
dc_shell=$(realpath -s -- "$dc_shell")
compat_lib=$(realpath -s -- "$compat_lib")
mkdir -p "$project_dir/results/dc_core_area"
run_dir=$(mktemp -d "$project_dir/results/dc_core_area/run.XXXXXX")
echo "Results: $run_dir"

for app in "${apps[@]}"; do
    for variant in baseline compensated processed; do
        out="$run_dir/${app}_${variant}"
        mkdir -p "$out"
        echo "Synthesizing ${app}_${variant}: $out/dc.log"
        (
            cd "$out"
            export DC_APP="$app" DC_VARIANT="$variant" DC_OUT="$out"
            export LD_LIBRARY_PATH="$compat_lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
            bash "$dc_shell" -f "$scripts_dir/synth_core_area.tcl" >dc.log 2>&1
        ) || { tail -n 60 "$out/dc.log" >&2; exit 1; }
        if ! grep -m 1 'Total cell area:' "$out/area.rpt" || ! test -s "$out/timing.rpt"; then
            tail -n 60 "$out/dc.log" >&2
            exit 1
        fi
    done
done
