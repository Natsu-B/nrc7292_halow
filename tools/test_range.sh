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
rm -f "$LOG_DIR/range.ready" "$LOG_DIR/range_result.json" "$LOG_DIR/source_clock.json"
# Cover the receiver's absolute deadline, including startup and extended loss phase,
# plus two seconds of post-test panic observation. Short overrides cannot reduce it.
monitor_seconds=$(python3 - <<'PY'
import math, os
rapid=os.environ.get('RANGE_RAPID','0')
if rapid not in ('0','1'):
    raise SystemExit('RANGE_RAPID must be 0 or 1')
rapid=rapid=='1'
defaults={'RANGE_STARTUP_SECONDS':45,'RANGE_HEALTHY_SECONDS':5 if rapid else 15,
          'RANGE_BAD_SECONDS':6 if rapid else 25,'RANGE_BAD_MAX_SECONDS':15 if rapid else 45,
          'RANGE_RECOVERY_SECONDS':12 if rapid else 30,'RANGE_IDLE_SECONDS':30}
values={name:float(os.environ.get(name,default)) for name,default in defaults.items()}
if any(not math.isfinite(value) or not 1 <= value <= 300 for value in values.values()):
    raise SystemExit('Range phase durations must be 1..300 seconds')
if values['RANGE_BAD_MAX_SECONDS'] < values['RANGE_BAD_SECONDS']:
    raise SystemExit('RANGE_BAD_MAX_SECONDS must be at least RANGE_BAD_SECONDS')
minimum=math.ceil(values['RANGE_STARTUP_SECONDS']+values['RANGE_HEALTHY_SECONDS']+
    (2 if rapid else 1)*(values['RANGE_BAD_MAX_SECONDS']+values['RANGE_RECOVERY_SECONDS'])+10)+2
override=float(os.environ.get('RANGE_MONITOR_SECONDS',minimum))
if not math.isfinite(override) or not 0 < override <= 3600:
    raise SystemExit('RANGE_MONITOR_SECONDS must be positive and at most 3600')
print(max(minimum,math.ceil(override)))
PY
)
ESP_BY_ID=$("$ROOT/tools/find_usb.sh" esp)
console=(python3 "$ROOT/tools/hw_console.py" --port "$ESP_BY_ID" command)
"${console[@]}" pause
"${console[@]}" wiring-confirm
"${console[@]}" rf-confirm
"${console[@]}" connect --event connect --timeout 60
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
  --after-seconds "$monitor_seconds" | tee "$LOG_DIR/esp.log"
wait "$receiver"
python3 "$ROOT/tools/verify_range_clock.py" "$LOG_DIR" > "$LOG_DIR/source_clock.json"
printf 'Synthetic adaptation test result: %s/range_result.json\n' "$LOG_DIR"
