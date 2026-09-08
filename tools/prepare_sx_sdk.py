#!/usr/bin/env python3
"""Create a separate SDK 1.7 build tree. Never overwrite the user's original SDK.
The user must explicitly supply the module vendor's compatible BDF.
"""
from __future__ import annotations
import argparse, hashlib, json, pathlib, shutil, sys
ROOT = pathlib.Path(__file__).resolve().parents[1]

def sha(p: pathlib.Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()

def guarded_body(source: str, signature: str, replacement: str) -> str:
    if source.count(signature) != 1:
        raise ValueError(f"Unexpected SDK source: need exactly one {signature}")
    start = source.index('{', source.index(signature))
    level, end = 1, start + 1
    # The two known SDK functions contain no unmatched literal/comment braces.
    while level and end < len(source):
        level += (source[end] == '{') - (source[end] == '}')
        end += 1
    if level:
        raise ValueError("Unbalanced source function")
    body = source[start + 1:end - 1]
    return source[:start + 1] + '\n#ifdef CONFIG_L2BRIDGE_HSPI\n' + replacement + '\n#else\n' + body + '\n#endif\n' + source[end - 1:]

def prepare(sdk: pathlib.Path, output: pathlib.Path, bdf: pathlib.Path) -> None:
    sdk, output, bdf = sdk.resolve(), output.resolve(), bdf.resolve()
    if output.exists():
        raise ValueError("Output already exists; choose a NEW directory")
    if sdk == output or sdk in output.parents:
        raise ValueError("Output must not be inside the input SDK")
    version_file = sdk / 'VERSION-SDK'
    version = version_file.read_text()
    if not all(token in version for token in ['VERSION_MAJOR 1', 'VERSION_MINOR 7', 'VERSION_REVISION 0']):
        raise ValueError("Only the supplied SDK 1.7 layout is supported")
    if not bdf.is_file() or not 16 <= bdf.stat().st_size <= 65536:
        raise ValueError("Invalid BDF path/size")
    wlif = sdk / 'lib/lwip/port/netif/wlif.c'
    original = wlif.read_text()
    if 'CONFIG_L2BRIDGE_HSPI' in original:
        raise ValueError("Input SDK is already patched; use a clean archive")
    standalone = sdk / 'lib/modem/src/standalone.c'
    if 'wifi_lwip_init();' not in standalone.read_text():
        raise ValueError("SDK compatibility initialization is missing")
    patched = guarded_body(original,
        'static err_t low_level_output( struct netif *netif, struct pbuf *p )',
        '    (void)netif; (void)p;\n    /* No target-generated IP/ARP traffic. WPA uses its own driver path. */\n    return ERR_IF;')
    patched = guarded_body(patched,
        'void lwif_input(struct nrc_wpa_if* intf, void *buffer, int data_len)',
        '    extern void l2bridge_wifi_rx(uint8_t vif, void *buffer, int length);\n    if (intf != NULL) l2bridge_wifi_rx(intf->vif_id, buffer, data_len);\n    return;')
    # Keep modem/netif ABI initialization; do NOT patch standalone.c to remove it.
    shutil.copytree(sdk, output, ignore=shutil.ignore_patterns('.git', 'out', '.build-target', '__pycache__'))
    (output / 'lib/lwip/port/netif/wlif.c').write_text(patched)
    app = output / 'sdk/apps/L2BRIDGE_HSPI'
    shutil.copytree(ROOT / 'sx_newah/L2BRIDGE_HSPI', app)
    for name in ['hif.c', 'hif.h', 'hif_fifo.c', 'hif_hspi.c', 'hif_dma.h']:
        native = ROOT / 'third_party/newracom/target' / name
        sdk_native = sdk / 'sdk/apps/atcmd' / name
        if sha(native) != sha(sdk_native):
            raise ValueError(f"Native HSPI reference differs from supported SDK 1.7: {name}")
        shutil.copy2(native, app / name)
    # Ensure the on-wire ABI is byte-for-byte shared with ESP.
    for name, src in [('nrc_l2_protocol.c', ROOT/'components/nrc7292/src/nrc_l2_protocol.c'),
                      ('nrc_l2_protocol.h', ROOT/'components/nrc7292/include/nrc_l2_protocol.h')]:
        if sha(src) != sha(app/name):
            raise ValueError(f"Host/target protocol mismatch: {name}")
    destination_bdf = output / 'bdf/nrc7292/stage1_vendor.dat'
    shutil.copy2(bdf, destination_bdf)
    # SDK-wide IPv6 traffic is not needed by the IPv4-only bench demo.
    (app / '.config').write_text((app/'.config').read_text()+'\nCONFIG_IPV6 = n\n')
    record={'sdk_version':version.strip(), 'wlif_before_sha256':sha(wlif),
            'wlif_after_sha256':sha(output/'lib/lwip/port/netif/wlif.c'),
            'standalone_unchanged':sha(standalone)==sha(output/'lib/modem/src/standalone.c'),
            'vendor_bdf_sha256':sha(bdf), 'app':'L2BRIDGE_HSPI',
            'regulatory_status':'Vendor compatibility/certification must be confirmed; not established by this script.'}
    (output/'STAGE1_PREPARE.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))
    print(f"Prepared: {output}")

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=pathlib.Path,required=True,help='.../package/standalone from a clean SDK 1.7')
    p.add_argument('--output',type=pathlib.Path,required=True)
    p.add_argument('--bdf',type=pathlib.Path,required=True,help='Silex vendor-compatible nrc7292_bd.dat')
    a=p.parse_args()
    try: prepare(a.sdk,a.output,a.bdf)
    except (OSError,ValueError) as e: print(f"ERROR: {e}",file=sys.stderr);sys.exit(1)
