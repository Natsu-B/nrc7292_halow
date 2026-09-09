#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[[ ${STAGE1_WIRING_CONFIRMED:-0} == 1 && ${STAGE1_RF_CONFIRMED:-0} == 1 ]] || {
  echo 'Physical wiring and JP RF checks must already be confirmed.' >&2; exit 2;
}
: "${ESP_IP:?Set the actual ESP address}"
: "${PC_IP:?Set the AP-facing PC address}"
export VIDEO_PROFILES=${VIDEO_PROFILES:-"$ROOT/.build-range-video/video_profiles.bin"}
export LOG_DIR=${LOG_DIR:-"$ROOT/logs/range-$(date +%Y%m%d-%H%M%S)"}
mkdir -p "$LOG_DIR"
ESP_BY_ID=$("$ROOT/tools/find_usb.sh" esp)
console=(python3 "$ROOT/tools/hw_console.py" --port "$ESP_BY_ID" command)
"${console[@]}" pause
"${console[@]}" wiring-confirm
"${console[@]}" rf-confirm
"${console[@]}" connect --event connect --timeout 60
rm -f "$LOG_DIR/range.ready"
node "$ROOT/tools/test_range.cjs" > "$LOG_DIR/receiver.log" 2>&1 &
receiver=$!
cleanup() { kill "$receiver" 2>/dev/null || true; "${console[@]}" pause || true; }
trap cleanup EXIT
for ((i=0;i<100;i++)); do
  [[ -f "$LOG_DIR/range.ready" ]] && break
  kill -0 "$receiver" 2>/dev/null || { cat "$LOG_DIR/receiver.log"; wait "$receiver"; exit 1; }
  sleep .1
done
[[ -f "$LOG_DIR/range.ready" ]] || { echo 'Receiver did not bind' >&2; exit 1; }
# The monitor catches target panic/reboot as well as adaptation transitions.
"${console[@]}" start --event start --timeout 60 \
  --after-seconds "${RANGE_MONITOR_SECONDS:-100}" | tee "$LOG_DIR/esp.log"
wait "$receiver"
python3 - "$LOG_DIR" <<'PY' > "$LOG_DIR/source_clock.json"
import json, sys
from pathlib import Path
r = Path(sys.argv[1])
profiles = {}
for line in (r / 'esp.log').read_text(errors='replace').splitlines():
    try:
        event = json.loads(line)
    except ValueError:
        continue
    if event.get('event') == 'video_profile':
        profiles[event['stream_id']] = event
frames = [e for e in map(json.loads, (r / 'range_timeline.jsonl').read_text().splitlines())
          if e['event'] == 'frame_match']
assert frames and profiles, 'Missing real frame/ESP profile evidence'
for frame in frames:
    profile = profiles[frame['stream']]
    source_pts = (frame['pts'] - profile['capture_epoch_ms']) % 12000
    assert any(ref['profile'] == profile['source'] and
               ref['frameIndex'] * (100 if ref['profile'] == 0 else 200) == source_pts
               for ref in frame['references']), 'Source clock was shifted or wrong frame selected'
epochs = {p['capture_epoch_ms'] for p in profiles.values()}
assert len(epochs) == 1, 'Capture clock restarted during quality changes'
print(json.dumps(dict(result='PASS', matched_frames=len(frames), streams=len(profiles),
                     capture_epochs=len(epochs), criterion='one unshifted recorded-source clock')))
PY
printf 'Synthetic adaptation test result: %s/range_result.json\n' "$LOG_DIR"
