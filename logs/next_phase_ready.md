# UDP and 360p readiness

Local checks only; no serial port opened, UDP port bound, or hardware changed by this check.

The current generated ESP configuration targets `192.168.10.20` and receiver `192.168.10.250:50000`, on SSID `halow_demo_video` with OPEN security. The PC address belongs to the AP-facing `enp0s31f6` interface identified by the root agent. Python 3 and Node are installed; these receivers require no npm installation, Electron, GUI, or ffprobe.

Run after custom association and both ping directions succeed:

```sh
cd /home/hotaru/silex/_codex_work/worktrees/stage1-real-hw-20260908-231300
export STAGE1_WIRING_CONFIRMED=1 STAGE1_RF_CONFIRMED=1
export ESP_IP=192.168.10.20 PC_IP=192.168.10.250 UDP_PORT=50000
UDP_SECONDS=10 LOG_DIR="$PWD/logs/udp-real" ./tools/test_udp.sh --drain-seconds 5
```

The rates are 100000, 250000, 500000 and 1000000 bit/s, in that order. Escalation stops at the first delivery failure. Each `udp_<rate>.json` records requested versus delivered rate, successfully sent versus received packets, duplicates/reordering/corruption, and independent sender Duty measurements. Missing packets are measured at the drain deadline, not claimed as modem RF PER. `requested_rate_met=false` remains distinct from a delivery PASS. A short interval entirely within a Duty pause can send zero packets; repeat the affected rate over a full 60-second interval if needed, using a new log directory:

```sh
UDP_SECONDS=60 LOG_DIR="$PWD/logs/udp-100k-full-window" ./tools/test_udp.sh --rates 100000 --drain-seconds 5
```

The existing `assets/video.bin` is a valid HVC1 clip containing 640x360 H.264, 120 frames over 12000 ms. File size is 265001 bytes; H.264 frame payload totals 263049 bytes. All reference frame CRCs validate. SHA256:

`90f07b05a126b786689d3b8bb6200eda6c98606f7c2c7bfa11a68c7797be87a8`

The local built partition table has a valid MD5 and a 2 MiB `video` partition at `0x200000`; the clip fits. This does not establish that the physical ESP video partition already contains this clip. If it needs writing, the root agent can run the existing ESP-only tool, which validates the actual partition table and performs byte-for-byte readback:

```sh
./tools/idf_env.sh python tools/flash_video.py \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_90:70:69:10:C7:B4-if00 \
  --clip "$PWD/assets/video.bin" --confirm-stage1-partitions
```

That operation resets ESP, so re-establish its HSPI/association session before continuing; it does not flash or reset SX. The video wrapper sends wiring/RF acknowledgements and `connect` for the new ESP boot itself.

```sh
VIDEO_CLIP="$PWD/assets/video.bin" VIDEO_SECONDS=60 LOG_DIR="$PWD/logs/video-360p-real" ./tools/test_video.sh
```

The receiver validates NRCV fragments and requires one continuous, same-stream, consecutive-frame cycle matching all 120 source frames in order and byte for byte. Successful `received_360p.h264` contains the actually received cycle. Results go to `video_result.json`; serial evidence goes to `esp_video.log`. Missing/corrupted/incomplete cycles do not pass. The offline video-cycle regression and shell syntax checks passed. The present ESP video pacing ceiling is 400000 bit/s and frame lateness limit is 250 ms; the 9444-byte maximum reference frame needs seven 1400-byte datagrams (about 196 ms at that ceiling), before RF/Duty or queue delays.
