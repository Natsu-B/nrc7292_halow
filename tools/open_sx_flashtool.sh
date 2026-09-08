#!/usr/bin/env bash
# The vendor _README_LINUX explicitly supports java -jar on this .exe.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
TOOL=/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool
[[ -n ${DISPLAY:-} ]] || { echo 'Run this in your graphical desktop terminal (DISPLAY is required).' >&2; exit 2; }
SX_BY_ID=$("$ROOT/tools/find_usb.sh" sx)
echo "SX identity: $SX_BY_ID -> $(readlink -f "$SX_BY_ID")"
echo 'SX programming and MAC WRITE OFF are manual actions in the GUI.'
echo 'Close the SX serial monitor before opening this tool.'
cd "$TOOL"
# Share the same advisory lock as sx_console.py; fail rather than steal replies.
exec flock -n -E 75 "$SX_BY_ID" java -jar FirmwareFlashTool.exe
