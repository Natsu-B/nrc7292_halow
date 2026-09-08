#!/usr/bin/env bash
# These are host-side stand-ins, NOT real ESP-IDF/Arm target builds.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only \
 -I"$ROOT/tests/esp_stubs" -I"$ROOT/components/nrc7292/include" -I"$ROOT/components/nrc7292/hspi" \
 -I"$ROOT/components/hvc_clip/include" -I"$ROOT/components/nrc_video_packet/include" -I"$ROOT/esp32/main" \
 "$ROOT/components/nrc7292/src/nrc7292.c" "$ROOT/esp32/main/app_main.c" "$ROOT/esp32/main/clip_source.c"
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only \
 -I"$ROOT/tests/nrc_stubs" -I"$ROOT/sx_newah/L2BRIDGE_HSPI" \
 "$ROOT/sx_newah/L2BRIDGE_HSPI/l2bridge_main.c"
for js in "$ROOT"/receiver/src/*.cjs "$ROOT"/receiver/ui/*.js;do node --check "$js";done
printf 'PASS: API-stub C syntax and real Node/renderer JavaScript syntax. Not a target link or Electron launch.\n'
