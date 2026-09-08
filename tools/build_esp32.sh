#!/usr/bin/env bash
# Build only. No flashing, erasing, credential export, or target re-selection on an existing config.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MENU=0
case "${1:-}" in --menuconfig) MENU=1;; '') ;; *) echo "Usage: $0 [--menuconfig]" >&2; exit 2;; esac
command -v idf.py >/dev/null || { echo 'BLOCKED: activate ESP-IDF v5.5.2 export.sh first.' >&2; exit 2; }
cd "$ROOT/esp32"
idf.py --version
if [[ -f sdkconfig ]]; then
  grep -q '^CONFIG_IDF_TARGET="esp32s3"$' sdkconfig || {
    echo 'Existing sdkconfig is not ESP32-S3. Back it up and select the target explicitly.' >&2; exit 2;
  }
else
  idf.py set-target esp32s3
fi
if (( MENU )); then idf.py menuconfig; fi
idf.py build
python3 "$ROOT/tools/collect_build.py" --esp-build "$ROOT/esp32/build" --output "$ROOT/esp32/build/artifact_manifest.json"
printf '\nBuilt from the current worktree. Review GPIOs, then idf.py -p PORT flash. SX flashing remains manual.\n'
