# Silex FlashTool の工場領域書込みに関する静的確認

2026-09-09。実ポートのオープン、GUI 起動、SX への RAM ロード・flash は行っていない。

**実機結果による訂正: 下記 CRC command `0x95` の静的評価は不十分だった。
root の実機試験では0x95送信後から応答が止まり、RAM pointer復元も失敗した。
0x95は使用禁止・再送しない。先行する44byte読取りとは切り分ける。**
この文書の監査担当は実機操作をしていないが、root実行結果を末尾へ追記している。

**2026-09-09 08:15 取得データによる判定: RF_CAL の移行はスキップされ、
version 更新は現在の SYS_CONFIG record と同一の内容を再保存する。
MAC 移行だけは、handler の実際の読取りアドレスが取得済み sector と異なるため未判定。**
下記「実機 factory record に基づく判定」に取得値と残る一条件を示す。
MAC WRITE OFF は指定した MAC の書込みを止めるが、ツールが先に実行する
MAC/RF calibration migration を止めない。custom firmware 側の migration guard は
別処理であり、FlashTool / 2nd bootloader の書込みを防げない。

## 確認対象

すべて提供された Silex 配布物から展開したもの。ディレクトリ:
`/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool`

| ファイル | bytes | SHA256 |
| --- | ---: | --- |
| `FirmwareFlashTool.exe` | 2350096 | `1ceb5c81e766bed58efe1e43d13a71e773b081acb86306cc8d3e43c80daaba38` |
| `firmware/nrc7292_boot.bin` | 10680 | `33db5706c1be366c07908eb0093ad5a6c1254778149415bf16971bc12c9c4057` |
| `firmware/nrc7292_boot_xip.bin` | 12924 | `51da4440d881fd77fdb91c11dcc8d19d81ffbfb76f68c2eaff07fc6826886be4` |

## GUI の実際の処理

`javap -c -p` で配布 Java class を確認した。

- `MainPanel.startOperation()` → `NewrafcFunctions.uploadXIP(...)`。
- accelerated 側は bytecode 333、通常側は 352 で `cmd_sf_migration()` を呼ぶ。
  いずれも 2nd bootloader を RAM へロード・実行した後、MAC WRITE の条件判定より前。
- `Newrafc.cmd_sf_migration()` は要求 `0x44`、応答 `0x45`。
  応答 option A / B はそれぞれ MAC / RF cal migration の done または skipped を示す。
- その後 `cmd_update_version()` も実行される。通常の START は読取り専用操作ではない。
- MAC0 / MAC1 の WRITE を OFF にしても上記の先行処理は残る。
- `MACAddressPanel` の READ は編集不可の結果表示欄。独立した READ ボタンはない。
  結果は通常 START の途中の MAC 読取りで表示される。
- accelerated を OFF にすること、両 Upload を OFF にすることによる migration 回避はない。
- 初期状態では XIP bootloader / firmware の Upload は双方 ON。
  `_FirmwareFlashTool.cfg` が存在すると保存設定を復元するため、再起動後の状態を
  常に初期値とみなすことはできない。確認時、その cfg は展開先に存在しなかった。

## 配布 2nd bootloader の 0x44 handler

ARM Thumb としてロード基点 `0x10488000` で逆アセンブルした。
dispatch `0x1048a144` が handler `0x10489bf0` を呼び、`0x10489cd8` が 0x45 応答を作る。

MAC 分岐:

1. flash 読取り関数へ相対値 `0xffffe008` と長さ 6 を渡し、marker `nrcnrt` と比較する。
2. 一致した場合、相対値 `0xffffe000` の 6 bytes を読んで MAC 保存関数
   `0x10489098` に渡す。SYS_CONFIG record の erase/write `0x10488c58` へ到達する。
3. 不一致ならこの MAC 移行分岐はスキップする。

RF cal 分岐:

1. RAM 上の flash map の RF_CAL アドレスから 124 bytes を読む。
2. 先頭 byte が 1 の場合、RF_CAL へ record 保存 `0x10488c58` を実行する。
3. region word `0x5355` (`US`) などの条件が成立すると、保存前にチャネル表の変換も行う。
   region が JP であっても先頭 byte が 1 なら record 再保存の分岐へ到達する。
4. さらに `0x104891f0` により SYS_CONFIG の migration flag を保存する。
5. 先頭 byte が 1 でなければこの RF cal 分岐をスキップする。

したがって、工場データの値が保持される可能性と、工場領域へ一切書かないことは別である。
初回監査時には現機の marker / RF_CAL / map が未取得だった。取得後の判定は末尾に記す。
RF_CAL と外部 BDF ファイルは同一物ではなく、BDF の不変確認だけでもこの処理は除外できない。

## XIP upload の範囲

Java accelerated 側の呼出し引数を確認した。

- XIP bootloader Upload: erase 要求の開始 0、長さ `0x10000`。
  flash map record の `0xf000` もこの要求範囲内にある。
- XIP firmware Upload: 開始 `0x10000`、長さは firmware のファイル長。
  Silex UART firmware 855036 bytes の要求末尾は `0xe0bfc`。
- map loader `0x10488d08` は `0xf000` の record を読み、無効なら RAM に既定値を設定する。
  この関数内には map の flash 保存呼出しはない。

初回監査時には、今回の禁止事項を満たすと断定できる既存 GUI の組合せは見つからなかった。
read-only の独立した bootloader 問合せを今後検討する場合も、通常 START を流用せず、
RAM ロード前後の自動処理と送信 command を別途確認する必要がある。
この監査ではそのためのツール作成・実機実行はしていない。

配布 Java に読取りの部品は存在する:

- `cmd_load_mac_addr_req(index)`: `0x42` → `0x43`、MAC payload 6 bytes。
- `cmd_read_hardware_version()`: `0x52` → `0x53`。
- `cmd_load_sysconfig()`: `0x54` → `0x55`、SYS_CONFIG payload。
- `NewrafcFunctions.updateSysConfig(port, bootFile, null, null, false, hwVersion, observer)`
  は Java 上では 2nd bootloader ロード・実行・ping 後、条件付き MAC/hardware write を
  スキップし SYS_CONFIG を読む。migration 呼出しはない。ただし GUI にこの独立操作はなく、
  2nd bootloader 実行時の副作用・現機互換性を含めた読取り専用の実証は未実施。
- Flash / RAM 全域読取りの定数は存在するが、`Newrafc` にその実装済み読取り method は
  見つからなかった。配布ディレクトリに別の backup/readflash executable も見つからない。

## Linux 起動・ポート・電源順序

これらは操作方法の確認であり、工場値の保持判定は末尾の実機データに基づく。

- 配布 `_README_LINUX` は graphical 環境で `java -jar FirmwareFlashTool.exe` を指定する。
- serial dropdown は jSerialComm `getCommPorts()` → `getSystemPortName()` の一覧。
  編集可能には設定されていないため、Linux では `ttyUSBn` を選択する。
  スクリプトが表示する by-id の解決先と照合し、番号だけで対象を固定しない。
- `tools/open_sx_flashtool.sh` は by-id の存在が必要なので、SX が USB 電源 OFF の間は起動しない。
- ランチャーの advisory `flock` と同梱 Linux x86_64 jSerialComm 2.0.2 の自己競合は
  静的確認では見つからなかった。native `openPortNative` は `open`、
  `fcntl(F_SETFL, 0)`、termios 設定を使い、`flock` / `lockf` は使わない。
  `configPort` の `TIOCEXCL` はそれ以降の open を抑止する。
  USB を抜くと device inode が変わるため、GUI を終了してから power-cycle し、
  次の GUI は同じ by-id の再列挙後に起動する。実機でのロック動作は未試験。
- Silex Standalone guide WA107660XA p8 の USB 手順は Download mode → USB 接続して LED 点灯
  → Refresh / port 選択 → START。USB 接続前に GUI が必要という条件はない。
- 同 guide p7 は GUI 内 DIP 図の pin 1 が逆と明記。SW1 は Silex 表の
  Download `L H H L L H` を使用する。
- 同 guide p13 は USB または AC adapter による給電を示し、USB 例では CN1 が空いている。
  JP startup guide の printed p8 も CN2 を USB power connector と表示する。
  stock 構成では CN2 USB 給電だけで利用可能で、CN1 は必須ではない。

参考画像: `/home/hotaru/silex/_codex_work/pdf_pages/standalone-power-13.png`、
`/home/hotaru/silex/_codex_work/pdf_pages/evk-09.png`。

## 再確認コマンド

```sh
cd /home/hotaru/silex/_codex_work/flash/FirmwareFlashTool
javap -classpath FirmwareFlashTool.exe -c -p _Library.Firmware.NewrafcFunctions
javap -classpath FirmwareFlashTool.exe -c -p _Library.Firmware.Newrafc
javap -classpath FirmwareFlashTool.exe -c -p FirmwareFlashTool.Panels.MACAddressPanel
/home/hotaru/silex/_codex_work/gcc-arm-none-eabi-7-2018-q2-update/bin/arm-none-eabi-objdump \
  -b binary -m arm -D -M force-thumb --adjust-vma=0x10488000 \
  --start-address=0x10489bf0 --stop-address=0x10489cf2 firmware/nrc7292_boot.bin
```

これらはローカルファイルを読むだけで、serial port へアクセスしない。

## 追補: 配布 2nd bootloader の起動から command 待ちまで

同日、上記 SHA256 の `nrc7292_boot.bin` を追加確認した。
**この起動経路から工場データ配列の erase / program / migration への呼出しは確認されなかった。**
ただし以下の通り flash 制御 register の設定命令はあり、「すべての flash 操作が読取りだけ」
という意味ではない。実機のデータ比較は未実施。

| 段階 | アドレス / 処理 | 確認内容 |
| --- | --- | --- |
| Vendor RAM load / run | `0x10488000` | Java `cmd_store_mem_stream` と `cmd_run` の引数が双方 273186816 |
| Vector | SP `0x104b8000`、entry `0x10489e1d` | Thumb Reset_Handler は `0x10489e1c` |
| Reset_Handler | `0x10489e1c` | SoC register 設定、RAM data copy / BSS clear、`0x1048a1ec` へ |
| main | `0x1048a1ec` | 下記初期化後、`0x10489914` の呼出しを繰返す |
| JEDEC read | `0x10488bbc` → `0x10488470` | flash command `0x9f` を発行して ID を読む |
| Baud / UART | `0x10489a98`、`0x10488178` | RAM の baud override marker を読む、未指定時 460800、UART3 初期化 |
| Flash setup | `0x10488e70` | JEDEC に応じて function table を RAM へ設定、geometry / map load、driver init |
| Geometry | `0x10488a94` | RAM に容量などの値を設定、erase / program の呼出しなし |
| Map load | `0x10488d08` | flash `0xf000` record を検査。無効な場合も既定 map を RAM へ設定するだけ |
| Read callbacks | `0x10489616` / `0x1048976a` → `0x104885bc` | どちらの flash driver 系統も read 関数へ解決する |
| Packet callback 登録 | `0x104897f4` | RX / TX / command dispatch の関数 pointer と packet state を RAM へ設定 |
| Packet wait | `0x10489914` | UART byte を読み、SOH/STX/ETX/EOT、長さ、checksum を検査してから dispatch |

command dispatch `0x10489efc` に達するのは packet 検証後であり、起動時の自動 migration、
timeout による flash 書込み、timeout による既存 XIP firmware の自動起動は見つからない。
実装する問合せ側では送信 command を限定し、通常 GUI START を呼ばない必要がある。

起動時の flash 制御設定について:

- main は driver init に `r1=0, r2=0` を渡す。
- `0x10489428` 系統のその分岐では、一部 JEDEC ID に対し制御命令が存在する。
  ID `0xc22816` は `0x104888a0`、ID `0x1c3816` は `0x10488660` を経由し、
  `0x06` 後にそれぞれ `0x01` / `0xc1` を含む制御 transaction を発行する。
  別の特定 ID では `0x04` も発行する。
- `0x10489698` 系統は `0x06` と `0x98` を含む設定 / 保護解除 transaction を発行する。
- RAM map / SYS_CONFIG / RF_CAL record 保存 `0x10488c58`、map 保存 `0x10488e58`、
  MAC 保存 `0x10489098`、migration `0x10489bf0`、array program `0x104884ec`、
  erase callback は、この起動時の制御分岐からは呼ばれていない。

よって、flash 制御状態を含む無副作用動作とは呼ばず、工場データ配列を変更しない
RAM boot / command 待ちの静的経路として扱う。今後の読取り command 自体は別の確認対象。

起動経路の主な再確認範囲は `0x10489e1c..0x10489e88`、`0x1048a1ec..0x1048a23c`、
`0x10488d08..0x10489008`、`0x10489428..0x104897a4`、`0x104897f4..0x10489a10`。
上記 `objdump` の start / stop address を置換して再現できる。

## 追補: RAM map pointer を使う工場データ読取り案

同じ配布 2nd bootloader の限定経路を独立確認した。以下は静的監査であり、
この文書の作成時点で実機実行・factory dump 一致は未確認。

単純な LOAD_MEM `0x27` は RX payload へ読んだ後に TX payload へコピーしないため、
応答が stale data になる可能性がある。LOAD_FLASH `0x29` も実読取りが stub であり、
これらは backup の証拠に使用しない。

代わりに、RAM 上の SYS_CONFIG offset を一時的に変えて既存の SYS_CONFIG READ を使う:

1. MAP READ `0x58` → `0x59` で RAM map 56 bytes を取得し、元の SYS_CONFIG offset を保存する。
2. STORE_MEM `0x23` で `0x10498054` へ little-endian 4 bytes の flash offset を書く。
   これは RAM のみの変更で、map の flash 保存命令 `0x56` は送らない。
3. SYS_CONFIG READ `0x54` → `0x55` で、その flash offset の raw 44 bytes を取得する。
4. 読取り完了・例外終了時には元の RAM offset を復元し、MAP READ で復元を確認する。

### Command とメモリ範囲の根拠

| 対象 | 確認した経路 |
| --- | --- |
| STORE_MEM `0x23` | dispatch `0x10489fae` → `0x10489a10`。指定長の単純 byte copy。対象を RAM の4bytesに限定すれば flash 関数への呼出しなし |
| STORE_MEM response | `0x10489a26` が `0x24` 応答を構築 |
| SYS_CONFIG RAM offset | map base `0x10498034` の byte offset32、すなわち `0x10498054..0x10498057` |
| SYS_CONFIG READ `0x54` | dispatch `0x1048a174` → `0x10489d9c`。map[32]から44bytesを `0x10488be4` 経由で raw read |
| Raw read callback | `0x10489616` / `0x1048976a` → `0x104885bc`。`0x11000000 + offset` から RAM へ byte copy |
| SYS_CONFIG response | stack 上の44byte bufferをnonnullで `0x10489868` に渡す。TX payloadへ明示的にコピーし、総長60 / payload44の `0x55` 応答 |
| MAP READ `0x58` | dispatch `0x1048a1dc` → `0x10489ae8`。nonnullのRAM map pointerをbuilderへ渡し、総長72 / payload56の `0x59` 応答 |
| RAM map | `[0x10498034, 0x1049806c)` |
| RX packet / payload | `0x1049807c` / `0x10498089`。小さい要求 packet は RAM map と重ならない |
| TX packet / payload | `0x1049c08c` / `0x1049c099`。RAM map と重ならない |
| Boot image RAM load | `[0x10488000, 0x1048a9b8)`。10680bytesの固定配布物で、map/RX/TXとは重ならない |

SYS_CONFIG READ `0x54` は record 検査 `0x10488cb0` を呼ばず、指定された offset を
生の44bytesとして読む。内容が正しい SYS_CONFIG record でなくても CRC repair、
record 保存、migration へ分岐しない。MAP READ も RAM をコピーするだけ。

### 実装側で固定する条件

- 上記 SHA256 の boot image・固定 RAM load address を使う。
- STORE_MEM の許可先はその boot image 範囲と `0x10498054` の4bytesだけ。
- offset は flash 物理範囲内に限定する。0x54 は必ず44bytes読むので、最後の開始位置を
  `確認済みflashサイズ - 44` 以下とし、末尾は重複読取りと切出しで処理する。
- 元の map を保存し、pointer変更後は0x58で対象4bytes以外が変わっていないことも確認する。
- flash program / erase、migration0x44、version update0x46、flash map save0x56等を
  allowlist に入れず、通常 GUI START も呼ばない。
- RAM pointer の復元後に XIP 起動は自動実行しない。

この限定 command 経路から factory array の program / erase / migration への到達は
確認されなかった。起動時の flash 制御register設定については前節の制約が引き続き適用される。

## 追補: 容量 metadata と独立 CRC の読取り

`0x58` の map 56bytes は little-endian 14word。
word0 は PROFILE16bit / MAP_VERSION16bit、続いて FW, FW_INFO, CORE_DUMP,
USER_CONFIG_1..4, SYS_CONFIG, RF_CAL, FOTA, DEVICE_INFO, USER_DATA, FOTA_INFO。
容量フィールドはない。1MiB用の既定 map にも2MiB領域を指す FOTA 関連値が含まれるため、
最大 offset から物理容量を推定しない。

別の RAM `sf_info_t` は `0x1049806c` にあり、4word の順序は
block_size, sector_size, page_size, total_size。
`0x10488a94` が JEDEC ID に基づいてこの構造体を初期化する。
最初の3wordは 65536 / 4096 / 256、total_size は既知IDでは1/2/4/16MiB。
unknown ID では1MiBの既定値になるため、値は **loader reported capacity** と記録し、
JEDEC識別そのものや実測物理容量の証明と区別する。

RAM情報を得るためだけに、前節の pointer 操作に特別な固定値 `0xff49806c` を認める案:

```text
uint32(0xff49806c + 0x11000000) = 0x1049806c
```

raw read `0x104885bc` の32bit加算はこのRAMアドレスにwrapする。
0x54の44byte応答の先頭16bytesがsf_info、残り28bytesは隣接RX領域なので捨てる。
これはchip外flashを試読する処理ではなく、固定RAM metadataだけを読む。
一般の負値offsetや任意RAM/MMIOアドレスへ拡張しない。
geometryと既知サイズ集合を検査し、対象factory領域が得られた上限外なら停止する。
元のSYS_CONFIG pointerは読取り後に復元する。

以下の CRC command は静的に確認したが、**後の実機試験で失敗したため使用しない**:

- `0x95` dispatch `0x1048a1b6` → `0x10489ac0`。
- payloadは flash offset u32LE / length u32LE の8bytes。
- `0x11000000 + offset` とlengthを CRC関数 `0x10489dec` へ渡す。
- seed `0xffffffff`、byte単位のtable更新、最終反転。
  table `0x1048a5b0` は標準 reflected CRC32 の並び。
- `0x96` response の optionA がCRC、optionBは0、総長16、payload無し。
  SYS_CONFIG RAM pointerを参照せず、読取buffer経由のstale payload問題もない。
- この関数はmemoryを読むだけで、program/erase/migrationを呼ばない。
  ただしそのことは実行時のfaultを起こさない証明にはならなかった。
  当初のallowlist追加案を撤回する。

## 追補: 実 JEDEC 値の限定 RAM 観測候補

実機の loader reported capacity が1MiBだったため、既定値を物理容量と断定しないための
追加経路を調べた。`0x10488470` の呼出しは wrapper `0x10488bbc` からのみ、
wrapperの呼出しはmainの `0x1048a1f0` からのみだった。
mainは直後の `0x1048a1f4` でJEDEC結果をr5へ保存し、command待ちでも保持する。
sf_info / map / sf_opsにはJEDEC値の専用global fieldは見つからなかった。

固定配布binのlive stack frameを44bytes読む候補は、RAM `0x104b7fb8`、
前述raw readerへ渡す特別offsetは `0xff4b7fb8`。

| stack計算 | SP |
| --- | --- |
| binary vector初期SP (`00 80 4b 10`) | `0x104b8000` |
| Reset_Handler `0x10489e24` push `{r4,lr}` | `0x104b7ff8` |
| main `0x1048a1ec` push `{r4,r5,lr}` | `0x104b7fec` |
| main `sub sp,#20` | `0x104b7fd8` |
| packet wait `0x10489914` push `{r0,r1,r2,r4,r5,r6,r7,lr}` | `0x104b7fb8` |

このpacket-wait frameは0x54を処理する間もliveで、saved r5はframe+16
(`0x104b7fc8`)。ここがmainのJEDEC値。
同時に frame+12 のsaved r4が0、frame+28のsaved LRが`0x1048a22f`であることを
検証し、想定したframeでない場合は結果をJEDECとして採用しない。
後者はmain `0x1048a22a` のBLから戻るThumb addressである。

0x54 handlerの一時bufferはこのcall chainでは `0x104b7f48..0x104b7f74`。
観測元 `[0x104b7fb8, 0x104b7fe4)` とは重ならない。
RAMを任意走査せず、この固定metadata windowだけを対象とする。
map pointerは元へ復元・全体照合し、命令コード・vector・flash設定の変更はしない。

この節は静的候補の記録。保存JEDEC値の実機取得・frame検証が成功するまで、
実物理容量の識別成功と扱わない。既定容量の1MiBを測定値と呼ばない。

## 訂正: CRC 0x95 後の実機停止

root 実行ログ `logs/sx_factory_backup_20260909.log` と private manifest
`/home/hotaru/silex/_codex_work/private_sx_backups/20260909-074902-486311/manifest.json`
では、最初のsectorの二回読取り後のtarget CRCがtimeoutし、その後のRAM pointer復元も
確認できなかった。受信累計14612bytes、factory bin保存前に失敗、NV書込みcommand送信は0。

再監査で次の実行時条件を見落としていたことが判明した:

```text
packet RX payload = 0x10498089
CRC handler 0x10489ac6: ldr r2,[r0]    # source 0x10498089
CRC handler 0x10489acc: ldr r1,[r3,#4] # source 0x1049808d
```

payloadのoffset/lengthはu32 little-endianで、Readerの8byte payload形式と一致する。
しかしhandlerは両方を4byte非整列アドレスからword loadする。
NRC7292にはCortex-M0とM3が搭載されており、M0は非整列word loadでHardFaultとなる。
M3でも実行時設定などにより非整列faultが起き得る。
配布binのHardFault vectorは `0x10489e19`、実体 `0x10489e18` は無限loop。
従って、この非整列loadによるfaultは、CRCから応答が止まり以降の復元も応答しない
実測と整合する有力な原因である。実機のfault PC / core状態は取得できていないため、
正確なfault原因を実測で確定したとは記録しない。

0x95にflash program / erase命令がないという静的結論は維持されるが、
そこから実機実行の安全性を判断したのは誤りだった。0x95は削除し、再試験しない。
44byte READ `0x54` と RAM STORE `0x23` はbyte copyで、同じ非整列word-loadを使わない。
今後、二回一致した取得byteを保存する際もtarget CRC未検証であることを明記する。

## 実機 factory record に基づく判定（2026-09-09 08:15 取得）

root が取得した保存先:
`/home/hotaru/silex/_codex_work/private_sx_backups/20260909-081543-554105`。
この監査担当は保存ファイルだけを解析した。各 sector は二回の読取りが一致し、RAM map
復元も成功した。target CRC command は使用していない。以下の CRC は、保存済み record
に対して PC 上で `zlib.crc32(record[8:12 + payload_length])` を計算した結果である。
credentials や実 MAC 値はこの文書へ転記しない。

| 保存 sector | 取得データによる確認 |
| --- | --- |
| `memory_map_sector.bin`, offset `0xf000` | 4096 bytes 全体が `0xff`。現存する map record はない。 |
| `sys_config_sector.bin`, offset `0xfe000` | `nrct`、payload length 32、CRC `d0453c34` 一致。version 2、cal_use 1。MAC0 は非零、MAC1 は全零。record 後の sector 残部は全 `0xff`。 |
| `rf_cal_sector.bin`, offset `0xff000` | `nrct`、payload length 124、CRC `0624a7cf` 一致。**payload** version 1、country `JP`。record 後の sector 残部は全 `0xff`。 |

実行中 loader から取得した geometry は 1 MiB、別の固定 RAM 読取りで得た JEDEC は
`0xef6014`。RAM map の SYS_CONFIG / RF_CAL はそれぞれ `0xfe000` / `0xff000`。
FW_INFO は `0xf5000`。map 内には 1 MiB を超える未使用項目もあるため、それらを容量の
根拠にせず、取得範囲も拡張していない。

RF migration `0x44` は **raw sector** 先頭が 1 の場合にだけ書く。実機の先頭は `nrct` の
`n` (`0x6e`) なので、この分岐はスキップされる。JP calibration payload は変更されない。
payload の version 1 と raw sector 先頭を混同してはならない。

続いて必ず実行される version command `0x46` は handler `0x10489cf2` から
`0x1048901c(2)` を呼ぶ。現在の record を CRC 検証して payload 32 bytes を読み、先頭の
version word だけを 2 に設定し、record 保存 `0x10488c58` へ渡す。実機は既に version 2
なので、実ファイルからこの処理を再構成すると **44-byte record 全体が元と一致する**。
SYS_CONFIG sector の erase/write は発生するが、MAC0 / MAC1 / cal_use / hw_version を
含めた論理値と CRC は変わらない。これは工場 MAC 値の変更には当たらない。

XIP bootloader の erase `0..0x10000` に含まれる `0xf000` は既に空であり、現在の map
record を失わない。UART firmware の書込み要求は `0x10000..0xe0bfc`、sector 丸めでも
`0xe1000` までなので、SYS_CONFIG / RF_CAL とは重ならない。

### MAC 移行について残る正確な条件

取得した `0xfe000` sector の byte 8..13 は marker `nrcnrt` と一致しない。ただし、これだけで
実際の MAC migration がスキップされると断定した先行メッセージは訂正する。
handler の literal `0x10489cc8` は **`0xffffe008`**、MAC source literal `0x10489ccc` は
**`0xffffe000`**。raw callback `0x10489616` → `0x104885bc` は単に `0x11000000` を加えて
byte 読取りするため、実際の CPU source は **`0x10ffe008` / `0x10ffe000`** になる。
取得済み sector の CPU source `0x110fe000` とは異なる。

SDK header は SFC base `0x11000000` のみを示し、この二つの CPU アドレスが同一 flash
内容を読むという alias の証拠は確認できていない。1 MiB の JEDEC 容量だけでは、この
CPU mapping の同一性を証明できない。SDK header の `half_addr_sf` が translation unit
ごとに static 初期値 0 であることは負の literal の原因候補だが、断定はしない。

既存 GUI を MAC0 / MAC1 WRITE OFF で使う最終条件は、上記の **実際の MAC source**
で marker が不一致、または一致しても移行元 MAC が現在の MAC0 と同一と確認できること。
RF / SYS record の未取得や CRC 不明を理由とする保留は解消済みで、残るのはこのアドレス
一条件だけである。別アドレスの読取りを追加する場合は root が判断し、この監査担当は
target への送信を行わない。

CPUの一般仕様の一次資料:
[Newracom NRC7292 brochure](https://newracom.com/hubfs/Products/Product%20Brochure%20%28NRC7292%29%28NEWRACOM%29.pdf?hsLang=en)、
[Arm Cortex-M0 Generic User Guide](https://www.keil.com/dd/docs/datashts/arm/cortex_m0/r0p0/dui0497a_cortex_m0_r0p0_generic_ug.pdf)。
