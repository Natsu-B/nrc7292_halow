#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
export LOG_DIR=${LOG_DIR:-"$ROOT/logs/stage1-$(date +%Y%m%d-%H%M%S)"}
mkdir -p "$LOG_DIR"
finish() {
  status=$?
  "$ROOT/tools/collect_logs.sh" "$LOG_DIR.tar.gz" || true
  printf 'Evidence: %s; script exit=%d\n' "$LOG_DIR" "$status"
}
trap finish EXIT
[[ ${STAGE1_WIRING_CONFIRMED:-0} == 1 ]] || {
  echo 'NOT_TESTED: set STAGE1_WIRING_CONFIRMED=1 after the one-time physical check.' >&2; exit 2;
}
# SKIP_FLASH=1 is useful for repeated non-destructive tests of the same boot.
if [[ ${SKIP_FLASH:-0} != 1 ]]; then
  "$ROOT/tools/flash_esp.sh" 2>&1 | tee "$LOG_DIR/esp_flash.log"
fi
"$ROOT/tools/monitor_esp.sh" --seconds 5 2>&1 | tee "$LOG_DIR/esp_boot.log"
"$ROOT/tools/test_hspi.sh" "$@" 2>&1 | tee "$LOG_DIR/hspi.log"
if [[ ${TEST_NETWORK:-0} == 1 ]]; then
  : "${ESP_IP:?Set ESP_IP for PC-to-ESP ping}"
  : "${PC_IP:?Set PC_IP to the AP-facing Ethernet address}"
  [[ ${STAGE1_RF_CONFIRMED:-0} == 1 ]] || { echo 'RF NOT_TESTED: JP RF checks not confirmed.' >&2; exit 2; }
  ESP_BY_ID=$("$ROOT/tools/find_usb.sh" esp)
  python3 "$ROOT/tools/hw_console.py" --port "$ESP_BY_ID" command rf-confirm
  python3 "$ROOT/tools/hw_console.py" --port "$ESP_BY_ID" command connect --event connect --timeout 60 | tee "$LOG_DIR/association.log"
  ping -I "$PC_IP" -c 5 -W 2 "$ESP_IP" | tee "$LOG_DIR/pc_to_esp_ping.log"
  python3 "$ROOT/tools/hw_console.py" --port "$ESP_BY_ID" command "ping $PC_IP 5" --event ping --timeout 30 \
    | tee "$LOG_DIR/esp_to_pc_ping.log"
  "$ROOT/tools/test_udp.sh" 2>&1 | tee "$LOG_DIR/udp.log"
fi
if [[ ${TEST_VIDEO:-0} == 1 ]]; then
  "$ROOT/tools/test_video.sh"
fi
