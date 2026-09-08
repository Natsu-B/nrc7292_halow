# SX-NEWAH-EVK + ESP32-S3 Stage-1 ハードウェア初期設定

対象: `halow_stage1_v0.7.x` の `L2BRIDGE_HSPI` 構成。

## 重要: 通常動作では Hosted mode ではなく SA mode を使う

本構成では NRC7292 の内部 Flash に XIP standalone firmware (`L2BRIDGE_HSPI`) を書き込み、ESP32-S3 は外部 SPI master として HSPI で L2 frame をやり取りする。

したがって EVK の通常動作は **SA (Standalone) mode** で起動する。Linux/Raspberry Pi 用の `nrc7292_cspi.bin + nrc.ko` を使う Hosted mode とは別構成である。

Newracom Standalone SDK 自体に HSPI HIF があるため、Standalone = UART 専用という意味ではない。

## 1. SW1 設定

SX-NEWAH-EVK の SW1 は 1～6 番。必ず EVK の電源を切ってから変更する。

Silex Standalone Start-Up Guide の表を優先する。FirmwareFlashTool の画面図は DIP 1 番の表示が Silex EVK と逆なので、FlashTool 画面をそのまま真似しない。

| 用途 | SW1-1 | 2 | 3 | 4 | 5 | 6 |
|---|---:|---:|---:|---:|---:|---:|
| custom XIP FW 書込み | L | H | H | L | L | H |
| **Stage-1 通常動作 (L2BRIDGE_HSPI)** | **L** | **L** | **L** | **H** | **L** | **H** |
| Linux Host mode 参考 | L | H | L | L | L | H |

Silex ガイドの写真の向きでは、L/H は写真に示された上下方向に従う。筐体上の `ON` 表示だけで判断せず、Silex の表と写真を基準にする。

## 2. SX-NEWAH への custom firmware 書込み

1. ESP32-S3 と SPI 信号線を一旦外す、または S3 を確実に High-Z / reset にする。
2. SX-NEWAH-EVK の電源を OFF。
3. SW1 を Download: `L H H L L H` にする。
4. アンテナを接続する。
5. Silex 配布の `FirmwareFlashTool` を使用する。
6. `2ND BOOTLOADER PATH` は対応する `nrc7292_boot.bin`。
7. `XIP BOOTLOADER PATH` は対応する `nrc7292_boot_xip.bin`、Upload ON。
8. `XIP FIRMWARE PATH` はビルドした `L2BRIDGE_HSPI` XIP binary、Upload ON。
9. **MAC ADDRESS の WRITE はチェックしない。** 工場 MAC を変更しない。
10. EVK を USB または指定 AC adapter で給電して書込む。
11. 100% 完了後に電源 OFF。
12. SW1 を SA: `L L L H L H` に変更する。

USB-UART (CN4) を使って Download する場合、Silex ガイドの注意どおり USB ポートと CN4 USB-UART を同時接続しない。

## 3. HSPI 信号

SX-NEWAH モジュール側の信号は以下。

| Signal | SX-NEWAH module pin | 方向 (SX基準) | ESP32-S3 |
|---|---:|---|---|
| HSPI_CSN | 63 | Input | CS output |
| HSPI_CLK | 64 | Input | SCLK output |
| HSPI_MISO | 65 | Output | MISO input |
| HSPI_MOSI | 66 | Input | MOSI output |
| HSPI_EIRQ | 67 | Output | IRQ input |
| RST | 27 | Input | optional reset output |
| GND | 複数 | - | GND |

RST は **High = reset, Low = run**。モジュール内に約 200 kΩ の pull-down がある。

### Raspberry Pi 用 40-pin header を使う場合

Silex Host 構成と既存開発ログから、Raspberry Pi SPI0 系は以下として使われていた。

| Raspberry Pi signal | BCM GPIO | 40-pin physical | 用途 |
|---|---:|---:|---|
| SPI0 CE0 | GPIO8 | 24 | HSPI_CSN |
| SPI0 SCLK | GPIO11 | 23 | HSPI_CLK |
| SPI0 MISO | GPIO9 | 21 | HSPI_MISO |
| SPI0 MOSI | GPIO10 | 19 | HSPI_MOSI |
| GPIO5 | GPIO5 | 29 | HSPI_EIRQ |
| GND | - | 20/25/30/34/39 等 | 共通GND |

**注意:** 提供された Silex Start-Up Guide は、この 40-pin header 内部配線を信号名つきで表にしていない。したがって Stage-1 初回配線前に、EVK 回路図が入手できるなら照合し、なければ電源 OFF 状態で module pin 63～67 と header の導通を確認すること。上表は Raspberry Pi 標準 SPI0 と既存 Silex/Newracom Host 構成からの対応である。

GPIO7/CE1 は既存 Raspberry Pi overlay で設定対象になっていたが、`spi_cs_num=0` の試験では CS0(GPIO8)を使う。本 Stage-1 では GPIO7 は接続不要とする。

## 4. ESP32-S3 側の例

Stage-1 package の既定「例」は以下。使用する S3 board に合わせて変更する。

| ESP32-S3 example GPIO | 接続 |
|---:|---|
| GPIO12 | EVK HSPI_CLK |
| GPIO11 | EVK HSPI_MOSI |
| GPIO13 | EVK HSPI_MISO |
| GPIO10 | EVK HSPI_CSN |
| free input GPIO | EVK HSPI_EIRQ |
| free output GPIO (optional) | EVK RST |
| GND | EVK GND |

ESP32-S3 board によって GPIO の使用可否は異なるため、PSRAM/Flash/USB/JTAG/boot strap と競合しないピンを選ぶ。

EVK は ESP32-S3 の 3.3 V pin から給電しない。EVK は Silex 指定の USB/AC 電源で給電し、S3 とは GND を共通にする。

VDDIO/ロジック電圧は実機で確認してから結線する。SX-NEWAH module は VDDIO 1.8 V/3.3 V 系に対応するが、S3 GPIO は 3.3 V 系である。EVK の host interface が 3.3 V であることを DMM/回路図で確認する。

## 5. SPI 初期値

Stage-1 初回は次で開始する。

- SPI Mode 0
- 8 bit
- MSB first
- CS active-low
- clock: **5 MHz**
- EIRQ: 最初は polling でもよい
- EIRQを使う場合: **active-high level**

SX-NEWAH data sheet の HSPI clock 上限は 25 MHz。Silex Host 手順は 16 MHz を使用している。したがって 5 MHz → 10 MHz → 16 MHz の順で上げ、logic analyzer で ACK、CRC、slot sequence error が無いことを確認する。第一段階では 25 MHz を狙わない。

## 6. 推奨電源投入順序

### 最初の診断

1. AP-100AH を起動し設定完了まで待つ。
2. ESP32-S3 を起動。ただしまだ HSPI transaction を開始しない。
3. SX-NEWAH-EVK を SA mode で起動。
4. 500 ms～2 s 待つ、または S3 側が `NRC-HSPI` signature を retry して待つ。
5. S3 で HSPI register/signature test。
6. `NRC-HSPI`、slot size、slot count が正常なら Wi-Fi connect command を送る。
7. AP association 完了後、ping/UDP PRBS を開始。
8. 最後に映像 bin replay を開始。

S3 と EVK を同時投入する製品構成にしてもよいが、S3 firmware は SX 側の起動時間に依存せず、一定時間 signature を retry する設計にする。

## 7. RF bench 条件

- アンテナ未接続で送信しない。
- AP-100AH と SX-NEWAH を至近距離に置かない。Silex Standalone troubleshooting は接続先 AP と 3 m 以上離すことを案内している。
- 同軸直結するなら適切な減衰器を入れる。無減衰直結は禁止。
- SX-NEWAH data sheet は対向機からの入力をアンテナ利得込み -10 dBm 以下とするよう注意している。
- JP duty/LBT設定は実証でも有効にする。Duty を外して帯域試験する手順にはしない。

## 8. AP-100AH + PC

Stage-1 では AP-100AH は Silex 純正 firmware のまま使用する。

初回は変数を減らすため、固定チャネル・固定帯域幅・固定 SSID を使う。SX firmware、ESP32-S3、AP-100AH の SSID/認証/チャネル条件を一致させる。

PC は AP-100AH の Ethernet 側へ接続し、Stage-1 receiver を起動する。PC receiver は将来の ESP32-P4 でも同じものを使う。

## 9. 初回テストの合格条件

1. HSPI 5 MHz で `NRC-HSPI` signature を連続 100 回読める。
2. slot geometry が毎回同じ。
3. 10分連続で ACK/CRC/sequence error = 0。
4. AP association が再現可能。
5. 100 kbit/s UDP PRBS で loss 0 を確認。
6. 250/500 kbit/s、1 Mbit/s と段階的に上げる。
7. 360p H.264 bin が PC で完全再構成できる。
8. その後 1080p clip へ進む。

問題が出たら映像から逆算せず、HSPI → association → IP → UDP → video の順に切り分ける。

## v0.8.0 補足

上記のSW1・電源・配線条件は継続する。v0.8ではコンソールの`wiring-confirm`→`probe`→`diag`→`loopback 1000 1518`で診断を始める。画像binやAP接続はこの段階では不要。RF開始は別の`rf-confirm`→`connect`→`start`で行う。初期GPIO例を実配線と同一と決めつけない。実機用binはこのソース配布物に含まれない。
