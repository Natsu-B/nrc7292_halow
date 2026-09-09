#!/usr/bin/env bash
# Load the unchanged vendor GUI with identified SX and explicit image paths.
set -euo pipefail
umask 077
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
TOOL=/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool
CACHE="$ROOT/.build-sx-flashtool"
mkdir -p "$CACHE"
python3 - "$TOOL/FirmwareFlashTool.exe" "$CACHE" <<'PY'
import hashlib, pathlib, sys, zipfile
exe, cache = map(pathlib.Path, sys.argv[1:])
expected = '1ceb5c81e766bed58efe1e43d13a71e773b081acb86306cc8d3e43c80daaba38'
if hashlib.sha256(exe.read_bytes()).hexdigest() != expected:
    raise SystemExit('Unrecognized vendor GUI binary: launcher reflection has not been checked for this version')
for name, digest in {
    'nrc7292_boot.bin': '33db5706c1be366c07908eb0093ad5a6c1254778149415bf16971bc12c9c4057',
    'nrc7292_boot_xip.bin': '51da4440d881fd77fdb91c11dcc8d19d81ffbfb76f68c2eaff07fc6826886be4',
}.items():
    if hashlib.sha256((exe.parent / 'firmware' / name).read_bytes()).hexdigest() != digest:
        raise SystemExit('Supplied bootloader differs from the audited bytes: ' + name)
with zipfile.ZipFile(exe) as archive:
    for name in ('gson-2.8.5.jar', 'jSerialComm-2.0.2.jar', 'jxl-2.6.jar'):
        target = cache / name
        data = archive.read(name)
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
PY
CLASSPATH="$CACHE:$TOOL/FirmwareFlashTool.exe:$CACHE/gson-2.8.5.jar:$CACHE/jSerialComm-2.0.2.jar:$CACHE/jxl-2.6.jar"
javac -cp "$CLASSPATH" -d "$CACHE" "$ROOT/tools/SxFlashToolLauncher.java"
if [[ ${1:-} == --check ]]; then
  exec java -Djava.awt.headless=true -cp "$CLASSPATH" SxFlashToolLauncher --check
fi
[[ $# -le 1 ]] || { echo 'Usage: open_sx_flashtool.sh [XIP_FIRMWARE_BIN | --check]' >&2; exit 2; }
[[ -n ${DISPLAY:-} ]] || { echo 'Run this in your graphical desktop terminal (DISPLAY is required).' >&2; exit 2; }
SX_BY_ID=$("$ROOT/tools/find_usb.sh" sx)
BOOT="$TOOL/firmware/nrc7292_boot.bin"
XIP_BOOT="$TOOL/firmware/nrc7292_boot_xip.bin"
XIP_FW=$(realpath -e -- "${1:-${SX_XIP_FIRMWARE:-/home/hotaru/silex/nrc7292_xip_fw_sx_uart.bin}}")
[[ -f "$BOOT" && -f "$XIP_BOOT" && -f "$XIP_FW" ]] || { echo 'A firmware image is missing.' >&2; exit 2; }
mkdir -p "$ROOT/logs"
LOG="$ROOT/logs/sx_flashtool_$(date +%Y%m%d-%H%M%S)-$$.log"
# Vendor clearInputStream prints raw byte lines under DEBUG even with DEEP_DEBUG
# off. Remove those lines before both terminal and logfile output.
exec > >(sed -u '/^0x[[:xdigit:]][[:xdigit:]]$/d' | tee "$LOG") 2>&1
echo "GUI log: $LOG"
echo "SX identity: $SX_BY_ID -> $(readlink -f "$SX_BY_ID")"
sha256sum "$BOOT" "$XIP_BOOT" "$XIP_FW"
echo 'The GUI opens with MAC WRITE OFF. Press START manually.'
cd "$TOOL"
# Share the same advisory lock as sx_console.py; fail rather than steal replies.
exec flock -n -E 75 "$SX_BY_ID" java -cp "$CLASSPATH" SxFlashToolLauncher "$SX_BY_ID" "$BOOT" "$XIP_BOOT" "$XIP_FW"
