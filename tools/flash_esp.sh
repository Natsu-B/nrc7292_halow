#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ESP_BY_ID=$("$ROOT/tools/find_usb.sh" esp)
BUILD=${ESP_BUILD_DIR:-"$ROOT/esp32/build"}
[[ -f "$BUILD/flasher_args.json" ]] || { echo 'ESP flash NOT_DONE: build first.' >&2; exit 2; }
# The USB identity is checked above; esptool additionally rejects a different chip.
"$ROOT/tools/idf_env.sh" python -m esptool --chip esp32s3 --port "$ESP_BY_ID" --after no_reset chip_id
"$ROOT/tools/idf_env.sh" idf.py -C "$ROOT/esp32" -B "$BUILD" -p "$ESP_BY_ID" flash
if [[ ${FLASH_VIDEO:-0} == 1 ]]; then
  "$ROOT/tools/idf_env.sh" python "$ROOT/tools/flash_video.py" --port "$ESP_BY_ID" \
    --clip "${VIDEO_CLIP:-$ROOT/assets/video.bin}" --confirm-stage1-partitions
fi
