#!/usr/bin/env bash
# Read-only tool inventory. This script never probes/opens serial ports.
set -uo pipefail
printf 'Host: '; uname -sm
for tool in idf.py xtensa-esp32s3-elf-gcc xtensa-esp-elf-gcc arm-none-eabi-gcc arm-none-eabi-g++ cmake ninja python3 node; do
  if command -v "$tool" >/dev/null; then printf 'PRESENT %s: %s\n' "$tool" "$(command -v "$tool")";
  else printf 'MISSING %s\n' "$tool"; fi
done
printf 'IDF_PATH=%s\n' "${IDF_PATH:-unset}"
printf '\nMissing compilers mean target binaries were NOT built. Native tests do not replace target builds.\n'
