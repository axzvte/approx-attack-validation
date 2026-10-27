#!/usr/bin/env bash
set -euo pipefail
EXE="$1"
DATA="$2"
TRIAL="$(mktemp -d "$(dirname "$EXE")/report-io-test-XXXXXX")"
ln -s /dev/full "$TRIAL/candidates.csv"
if "$EXE" "$DATA" "$TRIAL" 2 1 >"$TRIAL/console.log" 2>&1; then
    echo "Report write failure was incorrectly reported as success: $TRIAL" >&2
    exit 1
fi
echo "Report write failure correctly stops the trial"
