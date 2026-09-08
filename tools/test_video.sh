#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[[ ${STAGE1_RF_CONFIRMED:-0} == 1 && ${STAGE1_WIRING_CONFIRMED:-0} == 1 ]] || {
  echo '360p NOT_TESTED: physical wiring and JP RF checks must already be confirmed.' >&2; exit 2;
}
: "${ESP_IP:?Set ESP_IP to the configured ESP address}"
: "${PC_IP:?Set PC_IP to the AP-facing Ethernet address}"
export LOG_DIR=${LOG_DIR:-"$ROOT/logs/video-$(date +%Y%m%d-%H%M%S)"}
export VIDEO_SECONDS=${VIDEO_SECONDS:-30}
ESP_BY_ID=$("$ROOT/tools/find_usb.sh" esp)
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR/video.ready" "$LOG_DIR/video_result.json" "$LOG_DIR/received_360p.h264"
console=(python3 "$ROOT/tools/hw_console.py" --port "$ESP_BY_ID" command)
"${console[@]}" pause
"${console[@]}" wiring-confirm
"${console[@]}" rf-confirm
"${console[@]}" connect --event connect --timeout 60
node "$ROOT/tools/capture_video.cjs" >"$LOG_DIR/video_receiver.log" 2>&1 &
receiver=$!
cleanup() { kill "$receiver" 2>/dev/null || true; "${console[@]}" pause || true; }
trap cleanup EXIT
for ((i=0; i<100; i++)); do
  [[ -f "$LOG_DIR/video.ready" ]] && break
  kill -0 "$receiver" 2>/dev/null || { cat "$LOG_DIR/video_receiver.log"; wait "$receiver"; exit 1; }
  sleep .1
done
[[ -f "$LOG_DIR/video.ready" ]] || { echo 'Receiver did not bind' >&2; exit 1; }
"${console[@]}" start --event start --timeout 60 --after-seconds "$VIDEO_SECONDS" | tee "$LOG_DIR/esp_video.log"
wait "$receiver"
cat "$LOG_DIR/video_result.json"
