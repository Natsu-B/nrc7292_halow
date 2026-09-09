#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-"$ROOT/logs/stage1-$(date +%Y%m%d-%H%M%S).tar.gz"}
mkdir -p "$(dirname "$OUT")"
mkdir -p "$ROOT/logs"
OUT=$(realpath -m "$OUT")
files=(logs)
for file in STAGE1_RESULT.md RANGE_RESULT.md esp32/build/artifact_manifest.json .build-range-video/video_profiles.bin; do
  [[ ! -f "$ROOT/$file" ]] || files+=("$file")
done
# Configuration may contain WPA credentials; collect evidence, never sdkconfig.
args=(-C "$ROOT" --exclude='*.tar.gz' -czf "$OUT" "${files[@]}")
ACTIVE_LOGS=$(realpath -m "${LOG_DIR:-$ROOT/logs}")
if [[ -d "$ACTIVE_LOGS" && "$ACTIVE_LOGS" != "$ROOT/logs" && "$ACTIVE_LOGS" != "$ROOT/logs/"* ]]; then
  # Preserve an externally selected log directory under its full relative path.
  args+=(-C / "${ACTIVE_LOGS#/}")
fi
tar "${args[@]}"
sha256sum "$OUT"
