#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ESP_BY_ID=$("$ROOT/tools/find_usb.sh" esp)
exec python3 "$ROOT/tools/hw_console.py" --port "$ESP_BY_ID" hspi "$@"
