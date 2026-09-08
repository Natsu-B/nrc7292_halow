# SX factory readback helper

`tools/sx_read_factory.py` は専用の読取り補助ツール。SX flash の erase / program /
migration コマンドは送信しない。実機担当者だけが実行し、他の serial monitor や
FirmwareFlashTool を同時に開かない。

```sh
# 現在の UART へ vendor PING を一度送る。RAM load / reset はしない。
python3 tools/sx_read_factory.py probe

# SX を Download mode にした後、loader を RAM にだけロードして map / geometry を読む。
python3 tools/sx_read_factory.py ram-info

# 新たに Download mode へ電源再投入した後、限定した factory sectors を保存する。
python3 tools/sx_read_factory.py ram-read-factory

# ram-info 後、同じUSB enumerationのloaderが起動したままなら再ロードせず続行。
python3 tools/sx_read_factory.py resume-read-factory --resume-manifest /absolute/private/manifest.json

# 現機factory取得後、vendor migrationの正確なlegacy literalだけを調べる専用診断。
python3 tools/sx_read_factory.py resume-legacy-marker --resume-manifest /absolute/private/factory-manifest.json
```

RAM action は最初に ROM と 115200 baud で通信し、指定の 2nd bootloader を
`0x10488000` にロード・実行して 460800 baud へ移る。各 RAM action の終了後は
電源再投入する。明示的な `resume-read-factory` だけは、保存した成功manifestを指定して
同じ起動のまま続行できる。USB enumerationとlive map / geometryを再確認し、bootの
ロード・RUNを送らない。通常の RAM action や GUI START を同じ起動で実行しない。
loader 自身の初期化には flash status / unlock 操作があるため、すべての SPI flash
コマンドが読取りだけ、という意味ではない。factory array の書込みについては
[static audit](SX_FLASHTOOL_FACTORY_WRITE_AUDIT_20260909.md) を参照。

固定対象は CP2102N `10c4:ea60` / serial `08db3e3b2974ea11adf500fb6a30b0f3`。
boot image は 10680 bytes、SHA256
`33db5706c1be366c07908eb0093ad5a6c1254778149415bf16971bc12c9c4057`。

許可される要求は PING `0x21`、固定 image と一致する RAM STORE `0x23`、一度だけの
固定 RAM RUN `0x2b`、SYS_CONFIG READ `0x54`、RAM map READ `0x58`。
RUN 後の STORE は `0x10498054` の 4 bytes に限定する。

`0x95` は2026-09-09の実機で応答timeoutとなり、その後map復元も確認できなかったため
送信allowlistから削除した。RX payload `0x10498089` からの非整列32-bit LDRが
fault原因の候補。オフラインmodelの成功は実target実装の成功を意味しない。
再試行前に一度電源を切り、Download modeから `ram-read-factory` を直接実行する。

提供 loader の FLASH READ `0x29` は stub。MEM READ `0x27` は RX buffer へコピー
する一方、別の TX buffer を返すため使用しない。`0x54` は RAM map の SYS_CONFIG
offset から 44 bytes を読んで明示的に TX buffer へコピーする。offset を RAM だけで
変更することで指定 sector を読む。最後の 44-byte read は重複させ、sector 末尾を
超えない。元の pointer は finally で戻し、`0x58` の全 56 bytes 一致を確認する。

capacity は固定 offset `0xff49806c` だけを使い、loader の 32-bit 加算により
RAM `0x1049806c` の `sf_info_t` を読む。先頭 16 bytes 以外は破棄する。
任意 RAM read は提供しない。geometry は loader の JEDEC table による値であり、
未対応 ID では vendor の既定値になる可能性がある。容量外regionは明示的にSKIPPEDとし、
容量内で確認できたregionだけを読む。同じsectorを指すregionはaliasとして記録する。
`--flash-size` は任意の照合値として指定でき、loader 値と異なれば中止する。

factory action が保存するのは map sector、SYS_CONFIG、RF_CAL、DEVICE_INFO、
legacy MAC sector の各 4096 bytes。全 flash のバックアップではない。
各sectorは2回のbyte一致直後、次のtarget要求より先に0600で保存し、manifestもatomicに
更新する。SHA256とCRC32はhost上で計算した値。target CRCは `NOT_TESTED` と明示し、
成功扱いしない。取得完了は `acquisition_result: PASS`、全体は `result: PARTIAL`。
後続診断が失敗しても既に保存したsectorを消さない。

保存後の任意JEDEC診断は固定RAM offset `0xff4b7fb8` だけを読む。正確に固定した
loaderのwait frameでsaved r4=0 / LR=`0x1048a22f`が一致した場合だけsaved r5を
raw IDとして記録する。sentinel不一致はUNKNOWNとし、取得済みbackupを失敗にしない。
このIDからflash読取り範囲を自動拡大しない。元mapの復元確認は必須。

`resume-legacy-marker` は、取得完了・pointer復元済みのfactory manifestを使う。
`0xffffe000`というvendorの正確なliteralを44bytesだけ読み、`nrcnrt` markerの有無と
取得済みphysical SYS先頭44bytesとの一致だけを端末に出す。rawは0600で保存する。
この加算結果はCPU `0x10ffe000` であり、通常のSYS CPU `0x110fe000`とのaliasは
未確認。未マップならloaderが停止する可能性があるため、当該診断後の電源再投入が
予定されている実機担当者だけが明示的に実行する。任意addressの指定機能はない。

raw files は Git の外の `/home/hotaru/silex/_codex_work/private_sx_backups/` 配下へ、
directory mode 0700 / file mode 0600 で保存する。端末には読取内容や MAC / 認証情報を
表示せず、状態・offset・byte count・hash だけを出す。

```sh
# オフライン検証。実 USB / serial にはアクセスしない。
python3 tests/test_sx_read_factory.py
```
