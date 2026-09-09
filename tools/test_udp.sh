#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[[ ${STAGE1_RF_CONFIRMED:-0} == 1 && ${STAGE1_WIRING_CONFIRMED:-0} == 1 ]] || {
  echo 'UDP NOT_TESTED: physical wiring and JP RF checks must already be confirmed.' >&2; exit 2;
}
: "${ESP_IP:?Set ESP_IP to the configured ESP address}"
: "${PC_IP:?Set PC_IP to the AP-facing Ethernet address}"
ESP_BY_ID=$("$ROOT/tools/find_usb.sh" esp)
export LOG_DIR=${LOG_DIR:-"$ROOT/logs/udp-$(date +%Y%m%d-%H%M%S)"}
mkdir -p "$LOG_DIR"
python3 "$ROOT/tools/test_udp.py" --port "$ESP_BY_ID" --source "$ESP_IP" --bind "$PC_IP" \
  --udp-port "${UDP_PORT:-50000}" --seconds "${UDP_SECONDS:-10}" --log-dir "$LOG_DIR" "$@" \
  2>&1 | tee "$LOG_DIR/esp_udp.log"
