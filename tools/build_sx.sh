#!/usr/bin/env bash
# Compile an already-prepared SDK tree. Flash size is deliberately mandatory.
set -euo pipefail
if [[ $# != 3 ]]; then
  echo "Usage: $0 PREPARED_SDK TOOLCHAIN_BIN_DIR 2m|4m" >&2; exit 2
fi
SDK=$(cd "$1" && pwd); TC=$(cd "$2" && pwd); FLASH=$3
case "$FLASH" in 2m|4m);; *) echo 'Flash must be explicitly 2m or 4m' >&2;exit 2;; esac
[[ -f "$SDK/STAGE1_PREPARE.json" ]] || { echo 'Run prepare_sx_sdk.py first' >&2;exit 2; }
for name in gcc g++ ar objcopy objdump size nm strip readelf; do
  [[ -x "$TC/arm-none-eabi-$name" ]] || { echo "BLOCKED: arm-none-eabi-$name not found" >&2; exit 2; }
done
export PATH="$TC:$PATH"
cd "$SDK"
arm-none-eabi-gcc --version
make select target="nrc7292.sdk.release+xip+${FLASH}+stage1_vendor.dat"
# The vendor makefile invokes its own parallel child make.
make APP_NAME=L2BRIDGE_HSPI 2>&1 | tee stage1-build.log
python3 - "$FLASH" <<'PY'
import hashlib,json,pathlib,struct,sys
base=pathlib.Path('out/nrc7292/standalone_xip/L2BRIDGE_HSPI')
prefix='nrc7292_standalone_xip_L2BRIDGE_HSPI_'+sys.argv[1].upper()
elf=base/(prefix+'.elf');raw=elf.read_bytes()
if len(raw)<20 or raw[:4]!=b'\x7fELF' or raw[5]!=1 or struct.unpack_from('<H',raw,18)[0]!=40:
    raise SystemExit('Expected an ARM ELF, not a host test binary')
records=[]
for suffix in ['.elf','.bin','.map']:
    p=base/(prefix+suffix)
    if not p.is_file() or not p.stat().st_size:raise SystemExit('Missing/empty output: '+str(p))
    records.append(dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
pathlib.Path('stage1-artifact-manifest.json').write_text(json.dumps(dict(status='BUILD_OUTPUT_PRESENT_NOT_RF_VERIFIED',files=records),indent=2)+'\n')
print(json.dumps(records,indent=2))
PY
printf '\nBuild finished. Do not flash without confirming the EVK flash size, vendor BDF and bootloader compatibility.\n'
