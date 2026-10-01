# HaLow Stage1 v0.8.0: Native HSPI slave

ESP32-S3をSPI master、SX-NEWAH-EVK上のNRC7292を純正HSPI slaveとして動かす、地上映像伝送試験用ソースです。v0.7.1を基に更新しています。

2026-09-09にESP/NRC7292のbuildと実機通信を確認し、検証済みの変更をmainへ統合しました。ESP → native HSPI → SX → AP-100AH → Ethernet PCでの映像受信、画質・間隔の自動調整、再接続を確認しています。100m/s飛行・3km通信・実カメラ入力は未検証です。

- [STAGE1_RESULT.md](STAGE1_RESULT.md): USB識別、SX standalone、HSPI/EIRQ/loopback、IP/UDP・360pの基準試験。
- [RANGE_RESULT.md](RANGE_RESULT.md): 360p→120p、最大10秒間隔への調整と最新フレーム選択。
- [MOBILITY_RESULT.md](MOBILITY_RESULT.md): 100ms周期の判断、200ms周期の往復確認、悪化・回復の実機応答時間と最新の再現手順。

Gitにはソース、テスト、再現スクリプト、報告と確認済みのログを保存しています。MCUの実行bin/ELF、動画profileの生成物、sdkconfig、SDK/BDF、packet capture、受信動画はGit管理外です。生成したartifactのサイズ・SHA256は各報告とログに記録しています。`validation/v0.8.0/`と`validation/baseline_v0.7/`は実機試験以前の履歴です。

## 1. 構成と変更点

```text
ESP32-S3 FlashのHVC1映像
  → NRCV / UDP / IPv4 / esp_netif
  → ESP-IDF SPI master
  → NRC7292純正HSPI slave
  → L2BRIDGE_HSPI / 802.11ah
  → AP-100AH純正FW / Ethernet
  → 従来のPC Receiver
```

SX側では、添付Newracom SDK 1.7.0の`hif.c`、`hif_fifo.c`、`hif_hspi.c`をそのまま使用します。ESP側は同SDKの`nrc-hspi.c/h`を基にした移植版です。原本とSHA-256を`third_party/newracom/`に保存し、変更したホスト側コードと区別しました。

主な変更は次のとおりです。

- ESPのSPI操作を単一I/Oタスクへ集約し、送信途中にも受信キューを処理する。
- EIRQのHighレベル割込みを正しく設定する。ISRでは通知だけを行い、処理後に再許可する。割込み線未接続時のポーリングと、割込み取りこぼし用の100 ms監視も残す。
- 空きslot数0を正しく反映する。ACK、slotヘッダー、連番、キュー状態の異常を診断する。
- 転送途中のエラーで同じデータを無条件に再送しない。異常なホストセッションは停止し、NRC7292のDuty履歴を自動リセットしない。
- `probe`、`diag`、`loopback`を追加する。映像binやAP接続がなくてもHSPIを試験できる。
- SX側は、ホストがESP書込み等で不在になった際の空きslot待ちを通信破損と誤判定しない。送信中レコードを保持し、キューとメモリ量は固定上限内に抑える。
- `flash_video.py`は実機のパーティション表を読み、そのMD5、映像領域、クリップ全フレームのCRCを検証してから書く。書込み後は読戻し照合する。

### 純正slotとEthernetの境界は別

純正HSPIはslotに分割してバイト列を運びますが、アプリケーションのEthernetフレーム境界を保存するとは限りません。そのため既存の16 byte L2レコードヘッダーを残しました。type/length/sequence/CRCは制御応答の対応付けとデータ整合性確認にも使います。裸のEthernetフレームをMOSIへ流す実装ではありません。

NRCV映像packetの形式を維持し、PC Receiverには最新フレームの優先とNRCS v4 / NRPA / NRFB v3による往復確認・受信feedbackを追加しました。自動調整には更新済みのESP firmwareと本repoのReceiverを使用してください。HSPI loopbackにはSX側もv0.8.0のnative HSPI firmwareが必要です。

## 2. ビルド前の確認

```bash
bash tools/preflight.sh
bash tools/run_tests.sh /tmp/halow-native-v08
```

対象はESP-IDF v5.5.2とNewracom Standalone SDK 1.7.0です。Arm側はSDK指定の`gcc-arm-none-eabi-7-2018-q2-update`を基準にします。NixOSでは既存の開発用shell/FHS環境を使用してください。このソースはOS全体の設定を変更せず、コンパイラを自動インストールしません。

## 3. ESP32-S3用の実行binを生成

```bash
source /path/to/esp-idf/export.sh
bash tools/build_esp32.sh --menuconfig
```

通常の再ビルドは`bash tools/build_esp32.sh`だけです。既存sdkconfigを勝手に消したり、別ターゲットからS3へ変更したりしません。

確認済み配線はSCLK=GPIO9、MOSI=GPIO7、MISO=GPIO8、EIRQ=GPIO4、CE0=GPIO6、CE1=GPIO5、初期5 MHz・Mode 0です。CE0失敗時はCE1を独立に試し、同時assertを禁止します。EIRQはactive-high levelです。RSTはESPへ接続しておらず、SXのreset/power-cycleは人間が行います。

コンソールの既定値はS3の内蔵USB Serial/JTAGです。USB-UARTブリッジ側を使うボードでは、ESP-IDFのコンソール設定をそのUARTへ変更してください。

ビルド成功時の出力:

```text
esp32/build/halow_stage1.bin
esp32/build/halow_stage1.elf
esp32/build/bootloader/bootloader.bin
esp32/build/partition_table/partition-table.bin
esp32/build/flasher_args.json
esp32/build/artifact_manifest.json
```

`collect_build.py`は実行ELFがXtensa形式であることを確認します。ホスト試験用実行ファイルをESPファームウェアと見なすことはありません。

ESPのUSBシリアルが一意に識別できたら、ユーザーの許可範囲ではCodexが以下を実行できます。

```bash
cd esp32
idf.py -p /dev/serial/by-id/ESP_DEVICE flash
cd ..
python3 tools/flash_video.py --port /dev/serial/by-id/ESP_DEVICE \
  --clip assets/video.bin --confirm-stage1-partitions
```

フル消去は不要です。映像領域は、標準4 MiB Stage1表の0x200000から2 MiBです。`flash_video.py`は実機の0x8000のパーティション表を照合し、異なる配置なら拒否します。別の表オフセット、暗号化Flash、異なるパーティション構成は、このツールでは扱いません。

## 4. SX-NEWAH用コードをビルド

添付のSilex UART用binやLinux用`nrc7292_cspi.bin`は、本HSPI/L2ホストとは互換ではありません。

```bash
python3 tools/prepare_sx_sdk.py \
  --sdk /path/to/nrc7292_sdk-master/package/standalone \
  --output /path/to/NEW_sx_v08_build \
  --bdf /path/to/Silex/nrc7292_bd.dat

bash tools/build_sx.sh \
  /path/to/NEW_sx_v08_build \
  /path/to/gcc-arm-none-eabi-7-2018-q2-update/bin \
  2m
```

`2m`は例です。実物に合わせて`2m`か`4m`を選択します。Flash profileや校正領域は自動変更しません。2 MiB版で容量不足になる場合も、勝手に別profileへ移行しません。

ビルドが成功した場合のXIPアプリ出力:

```text
out/nrc7292/standalone_xip/L2BRIDGE_HSPI/
  nrc7292_standalone_xip_L2BRIDGE_HSPI_2M.bin
  nrc7292_standalone_xip_L2BRIDGE_HSPI_2M.elf
```

4 MiBでは末尾が`4M`です。ビルドスクリプトはARM ELFと実binの存在を確認し、`stage1-artifact-manifest.json`を生成します。

SDK 1.7を使ったSX native HSPI firmwareのbuild・人間によるflash・実機動作を確認しました。実際のbuild条件、factory領域を保護する適用方法とartifactのSHA256は [docs/SX_NATIVE_BUILD_20260908.md](docs/SX_NATIVE_BUILD_20260908.md) と [STAGE1_RESULT.md](STAGE1_RESULT.md) を参照してください。BDF・ライブラリ・ツールチェーンは再配布していません。

### 書込みは人間が行う

EVKの電源を切ってからSW1を変更します。

| 操作 | SW1-1 | 2 | 3 | 4 | 5 | 6 |
|---|---:|---:|---:|---:|---:|---:|
| XIP書込み | L | H | H | L | L | H |
| 通常運転：SA | L | L | L | H | L | H |

対応を確認したFlashTool・2nd/XIP bootloaderを使い、生成したL2BRIDGE_HSPIのXIPアプリを指定します。**工場MACを書き換えないでください。** Silex資料のDIP表を優先し、FlashToolの図から推測しません。USBとCN4 USB-UARTの同時接続禁止などの注意は`docs/EVK_HARDWARE_SETUP_ja.md`も参照してください。

## 5. 最初はRFを出さずに診断

起動しただけではSPIもAP接続も開始しません。配線・電源・I/O電圧を確認後、ESPコンソールで実行します。

```text
pins
wiring-confirm
probe
diag
loopback 1000 1518
```

`probe`はnative HSPIレジスタ、NRC-HSPIシグネチャ、queue geometry、L2 HELLOを確認します。`loopback`はSXのホストインターフェース内で1〜1518 byteを返す処理です。RF送信処理を呼びません。ただし以前の試験でAP接続済みなら、管理通信が残る可能性があるので、新規起動・未接続状態で診断してください。

`diag`と`loopback`はJSON行も出すため、Codexで収集できます。最初の合格条件は要求1000回に対してpassed=1000、bad_ack / bad_header / seq_gaps / spi_errors=0です。実機での合格はまだ得ていません。

EIRQ未配線でも診断は可能です。配線後はGPIO番号を設定し、IRQ主体の動作を確認します。電源断中のSXへESPが信号を駆動して逆給電しないよう、双方の電源順序・バス分離も確認します。

## 6. AP接続と映像

menuconfigでSSID、認証、パスワード、IPを実際のAP/PCに合わせます。例はESP=192.168.50.2、PC=192.168.50.10、UDP=50000です。送り先はAPの管理IPではなく、Ethernet側PCのIPです。

```text
rf-confirm
connect
status
start
pause
disconnect
```

`wiring-confirm`と`rf-confirm`はその起動中の確認であり、法規適合を保証する操作ではありません。SX側のJPガードは無効化できません。設定はSilex手順に合わせたDuty=60秒/6秒、margin=0µs、CS=128µs、送信後休止=2000µs、CCA=−83dBmです。Duty状態による欠落はSPI故障と分けて評価します。

PCアプリと映像assetsは従来版を同梱しています。Receiver内でH.264/MJPEGを復元・保存しますが、完成exeはありません。Node単体試験・localhost受信・C側との互換試験を再実行しました。Electron本体のGUI/H.264復号は今回未実行です。

`pause`は映像のみ停止します。`disconnect`やホストドライバ終了時にDutyを再設定・リセットしません。ただし明示的なNRCリセットや電源断では、FW内の履歴が失われる可能性があります。本版は飛翔用の永続airtime台帳やLoRa運用状態機械を含みません。

## 7. Gitへの取り込み

変更は作業環境内の専用worktreeで行いました。ユーザーのNixOS、USB機器、main branchは操作していません。

差分patchをユーザー側の新しいworktreeで`git apply --check`して適用し、対象MCUを実ビルド・書込み・試験してください。ユーザー変更がある場合に上書きやreset --hardはしません。mainへの取り込み条件とCodex向け手順は`docs/CODEX_HANDOFF_ja.md`にまとめました。
