#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-"$ROOT/.native-build"}
cmake -S "$ROOT/tests" -B "$BUILD" -G Ninja
cmake --build "$BUILD"
ctest --test-dir "$BUILD" --output-on-failure
(cd "$ROOT/receiver" && npm test)
"$BUILD/packet_fixture" "$ROOT/assets/test_h264.bin" "$BUILD/packets.fixture"
node "$ROOT/tools/check_c_node.cjs" "$ROOT/assets/test_h264.bin" "$BUILD/packets.fixture"
"$BUILD/packet_fixture" "$ROOT/assets/test_h264_1080p.bin" "$BUILD/packets1080.fixture"
node "$ROOT/tools/check_c_node.cjs" "$ROOT/assets/test_h264_1080p.bin" "$BUILD/packets1080.fixture"
"$ROOT/tools/check_syntax.sh"

python3 "$ROOT/tests/test_flash_tools.py"
python3 "$ROOT/tests/test_hw_tools.py"
python3 "$ROOT/tests/test_find_usb.py"
python3 "$ROOT/tests/test_sx_console.py"
python3 "$ROOT/tests/test_sx_read_factory.py"
node "$ROOT/tests/test_video_cycle.cjs"
python3 "$ROOT/tools/check_native_sources.py"
printf '\nNative tests finished. This does NOT build ESP or NRC firmware.\n'
