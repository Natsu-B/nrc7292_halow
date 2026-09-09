# Stage-1 最終実機報告

最新の高速な悪化・回復追従とmain再検証: [MOBILITY_RESULT.md](MOBILITY_RESULT.md)。

2026-09-09夜の追加変更（最新フレーム選択、360p→120p・最大10秒間隔への自動調整、切断後の再接続）とmain再検証は [RANGE_RESULT.md](RANGE_RESULT.md) を参照。以下は追加変更前のStage-1基準試験の記録。

更新: 2026-09-09 19:43 JST時点。ESP32-S3 → NRC7292 native HSPI → 802.11ah → AP-100AH → PC の通信と360p実映像を確認した。100/250/500 kbpsは配送・安定性PASS、1 Mbpsは139 packet欠落でFAIL。検証済みsourceをmainへ統合し、mainの再build・再flash・HSPI/EIRQ/loopback・双方向ping・UDP100k・360p byte一致・GUI実描画の再確認もPASS。

## 結果一覧

| 項目 | 結果 | 実測・根拠 |
|---|---|---|
| USB ESP32-S3 | PASS | 303a:1001、USB serial 90:70:69:10:C7:B4、esptoolでESP32-S3 rev0.2 |
| USB SX-NEWAH | PASS | 10c4:ea60、固有CP2102N serial、ROM PONGとRAM loader応答 |
| Ethernet AP-100AH | PASS | 192.168.10.10、Ethernet MAC 1c:bc:ec:1b:30:fe、HTTP200とモデル表示 |
| SX standalone boot | PASS | Silex standalone CLI、help、sf size=1048576応答 |
| SX → AP association | PASS | halow_demo_video、Open、wpa_state=COMPLETED、BSSID一致 |
| AP → PC Ethernet | PASS | PCのenp0s31f6からAPへping 3/3 |
| Standalone SX ↔ PC ping | PASS | 両方向5/5。SPI試験とは独立したRF経路の証拠 |
| custom SX build | PASS | NRC7292 / native HSPI / L2BRIDGE_HSPI、ARM link済み、832972 bytes |
| custom SX flash | PASS | 人間によるGUI Complete、指定guarded bin、MAC/RF migration skipped |
| custom SX boot | PASS | NRC-HSPI、期待ID、HELLOとloopbackで動作確認 |
| ESP build | PASS | ESP-IDF v5.5.2、esp32s3。main fresh buildもPASS、35設定項目一致 |
| ESP flash | PASS | 固有by-idで書込み、hash照合。main版も再flash済み |
| ESP boot | PASS | main版app version d35344c、実bootとconsole応答 |
| CE0 GPIO6 probe | PASS | 5 MHz、chip0x7292 / modem1 / sw0x800 / board0x07020902 / NRC-HSPI |
| CE1 GPIO5 probe | NOT_TESTED | CE0成功のためfallback不要。未試験を故障扱いにしない |
| HSPI 1MHz | NOT_TESTED | 5 MHz成功のため低速fallback未使用 |
| HSPI 5MHz | PASS | native signature、RX/TX各32×512、HELLO、実slot転送 |
| EIRQ GPIO4 | PASS | active-high level。main再試験のIRQ/High検出count各1372 |
| loopback 1518×1000 | PASS | pollingとEIRQで1000/1000。main EIRQ再試験12078 ms、各種error0 |
| AP association custom FW | PASS | SDK接続成功後のACK、link=1、RSSI取得。main再試験もerror0 |
| IPv4 ping | PASS | 修正後両方向5/5。main ESP ping完了後も3秒監視、panicなし |
| UDP 100k | PASS | 367/367、375808 bytes一致。main再試験も同値、安定性PASS |
| UDP 250k | PASS | 916/916、loss/corruption0、Duty停止計測0 ms |
| UDP 500k | PASS | 1832/1832、loss/corruption0、Duty停止計測0 ms |
| UDP 1M | FAIL | 2125送信/1986受信、欠落139、同区間SX queue drop139 |
| 360p | PASS | 同一stream・連続120 frameの順序/byte一致。main再試験も同一SHA256 |
| Electron GUI | PASS | 実UDPのH.264をWebCodecsで復号・描画。main再確認では表示330→355 frame |
| 1080p | NOT_TESTED | 360p実証まで実施 |
| merged to main | YES | 検証済みsourceをmainへ統合 |
| merge commit | d35344cadb13203c30a9dfd23ddc061952292bef | 実機再検証対象source。後続の文書・証跡のみのcommitとは区別する |

PASSはその行の試験範囲に限る。Standalone成功、native SPI成功、UDP配送、安定性、映像byte一致、画面描画を別々に確認した。1 Mbpsを含む通し試験の終了値は1であり、全負荷PASSとは扱わない。

## 接続機器と固定構成

- ESP: `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_90:70:69:10:C7:B4-if00`
- SX: `/dev/serial/by-id/usb-Silicon_Labs_CP2102N_USB_to_UART_Bridge_Controller_08db3e3b2974ea11adf500fb6a30b0f3-if00-port0`
- ESP Master → NRC7292 native HSPI Slave: SCLK GPIO9、MOSI GPIO7、MISO GPIO8、EIRQ GPIO4、CE0 GPIO6、CE1 GPIO5。Mode0、8 bit、MSB first、CS active-low。CE0/CE1を同時にLowにしない。RSTは未接続で、SX resetは人間が実施。
- 共通GND・両アンテナ・SW1・CN8ジャンパーを人間が確認。JP2は3.3V設定位置として確認し、電圧を実測したとは扱わない。SXはCN2 USB給電、ESPからの3.3V追加配線なし。
- AP: AP-100AH、firmware BB3-5.10 (2024.06.14)、SSID `halow_demo_video`、Open、925.5 MHz / 2 MHz、無線BSSID `1c:bc:ec:1b:30:ff`。Standaloneのwpa表示freq=5190はAPの実RF表示と混同しない。
- PC: `192.168.10.250/32`、`enp0s31f6`。AP `192.168.10.10/32` とSX/ESP `192.168.10.20/32` に有線host routeを設定。ESPは `192.168.10.20/24`、gateway `0.0.0.0`、UDP宛先 `192.168.10.250:50000`。インターネットのdefault routeは既存Wi-Fiを維持した。
- JP Duty/LBTは無効化していない。BDFファイルも変更していない。SX GUIではMAC WRITE OFFとMAC/RF migration skippedを確認。標準GUIの同値SYS情報再保存処理を監査したため、「工場領域へのerase/programが一切なかった」とは主張しない。限定factory backupはGit外・0600で保存している。

## 実書込みfirmware

main再検証対象ESP:
`/home/hotaru/silex/halow_stage1_main/esp32/build/halow_stage1.bin`

365456 bytes、SHA256:
`a1699c71ea2800e1670a633aa09d08447692e6fe8030fe6d40203235efa5f49a`

SX custom:
`/home/hotaru/silex/_codex_work/prepared_sx_20260908_guarded/out/nrc7292/standalone_xip/L2BRIDGE_HSPI/nrc7292_standalone_xip_L2BRIDGE_HSPI_2M.bin`

832972 bytes、SHA256:
`c05ec7b2bdf3dc94ccba87ac8d543bdd2db1cc4e5ee7ae770df2f57d89fe6a01`

独立RF確認に使用したSilex UART standalone:
`/home/hotaru/silex/nrc7292_xip_fw_sx_uart.bin`

SHA256:
`7e93f02fb67a0d6504998a7372dc01cdcaf47c800c70f5b8224844dc85170ccf`

映像partitionのHVC1は `assets/video.bin`、640×360 / 120 frame / 12秒 / 265001 bytes。
SHA256: `90f07b05a126b786689d3b8bb6200eda6c98606f7c2c7bfa11a68c7797be87a8`。
実partitionを読んで検証し、`0x200000` のvideo領域へ書込み・byte readbackを照合した。ESP実Flashは8 MiBだが、現在のpartition/headerは既存の4 MiB構成を使用する。

build manifestの `BUILD_OUTPUT_PRESENT_NOT_HARDWARE_TESTED` はbuild収集ツール単体の記述であり、実機試験結果は本報告と対応runtime logに記録する。

## UDP・映像の実測と限界

各UDP負荷は30秒。Duty時間はSXから受けた送信可否のsampling値であり、packet lossから推定していない。

| 要求bit/s | 送信 | 受信 | 欠落 | 破損 | Duty停止計測ms | SX queue drop | 配送 / 安定性 |
|---|---:|---:|---:|---:|---:|---:|---|
| 100000 | 367 | 367 | 0 | 0 | 0 | 0 | PASS / PASS |
| 250000 | 916 | 916 | 0 | 0 | 0 | 0 | PASS / PASS |
| 500000 | 1832 | 1832 | 0 | 0 | 0 | 0 | PASS / PASS |
| 1000000 | 2125 | 1986 | 139 | 0 | 12574 | 139 | FAIL / PASS |

1 Mbpsでは1538送信機会をDutyで見送り、送信済みpacketの欠落139とSX queue drop139が一致した。受信実効速度は542310.4 bit/s、欠落率は約6.54%。Duty停止と送信後の欠落を別計数し、制限解除や再送による隠蔽は行っていない。詳細は `logs/stage1-fixed-20260909/udp_*.json`。

360pの60秒試験は1379 packet、597完成frame、不正/重複/順序異常/失効/reference mismatch各0。同一streamのframe0..119を元HVC1の順序・byteと完全照合した。`received_360p.h264` は実受信した263049 bytesで、独立した再比較も一致した。
SHA256: `d6167cdd1046e2874bd440ed26026c0886957929a515fa09b6df2cebbf4cdd7c`。
詳細は `logs/video-360p-real-20260909/video_result.json`。動画中の累積target drop139は直前の1 Mbps試験から増えていない。

byte確認後、既存Nix Electron **40.10.3** で実UDPを受信し、H.264 `avc1.42c016` をWebCodecsで復号・描画した。表示frame440→465を `logs/gui_live_confirmation_20260909.json`、対象アプリだけの画像を `logs/receiver_360p_live_20260909.png` に記録した。main再検証でも実描画を確認し、GUIを受信・表示中のまま残した。package.json指定44.2.0による配布buildは未試験で、追加installはしていない。SNR API値は表示したが取得成功flagはprotocolにないため、`snr_validity=unreported` として扱う。

## mainからの再検証

source commitは `d35344cadb13203c30a9dfd23ddc061952292bef`。mainを新規buildし、worktreeで試験した35設定項目との一致を確認してESPへ再flashした。

| main試験 | 結果 | 根拠 |
|---|---|---|
| fresh build / flash / boot | PASS | 上記SHA256、app version d35344c |
| CE0 / 5 MHz probe / HELLO | PASS | chip0x7292、NRC-HSPI、RX/TX各32×512 |
| EIRQ loopback 100×64 | PASS | 402 ms |
| EIRQ loopback 100×512 | PASS | 606 ms |
| EIRQ loopback 1000×1518 | PASS | 12078 ms、IRQ/High count各1372、SPI/ACK/header/sequence/status error0 |
| custom association | PASS | connect error0 |
| PC → ESP ping | PASS | 5/5、平均15.346 ms |
| ESP → PC ping | PASS | 5/5、stack5120/min_free2228 bytes、終了後3秒panicなし |
| UDP100k / 30秒 | PASS | 367/367、375808 bytes、100215.47 bit/s、全error/drop/Duty停止0、終了後2秒安定 |
| 360p / 60秒 | PASS | 1380 packet、597照合frame、不一致/不正/重複/順序異常/失効/reference drop0。連続120 frame、263049 bytes、原試験と同一SHA256 |
| GUI再表示 | PASS | 2.5秒で表示330→355 frame、640×360、H.264 avc1.42c016、loss0%、errorなし、実test patternを目視確認 |

主な再検証log: `logs/main_hspi_20260909.log`、`logs/main-udp-20260909/udp_100000.json`、`logs/main-video-360p-20260909/video_result.json`、`logs/main_gui_live_confirmation_20260909.json`。対象アプリの画像は `logs/main_receiver_360p_live_20260909.png`、mainのbuild manifestは `logs/main_artifact_manifest_20260909.json`。後続の文書・証跡のみのcommitとは区別し、firmware実機検証のsource M1は上記merge commitである。

## 修正した不具合と検証

- polling probeの成功cacheだけで再probeがPASSする欠陥を修正。I/O session開始前のfresh/reprobeではchip/signature/queueを実読取りし、target resetは行わない。active I/O session中のCLI probeはcached identityであることと同期再読取りを行わないことを明示する。成功後に無応答へ切り替える回帰試験は修正前FAIL、修正後PASS。mainではfresh reset後の実probeと実データloopbackを確認した。
- 実機でping5/5の成功event直後にping task stack overflowを再現。stackを2560→5120 bytesへ明示設定し、修正版で最小空き2228 bytesと終了後安定性を確認した。
- host automationもstack overflow/Guru Meditation/abort/assert/Rebootingを検出して失敗するよう修正。ping/UDP完了後も監視し、UDP配送PASSと不安定な全体結果を混同しない。
- CE切替、native transport、UDP pattern/CRC、映像の単一連続cycle、USB個体識別、SX読取りallowlistなどのhost回帰試験はPASS。C12/12・Node12/12と追加Python/互換/ソース整合性試験の証跡は `logs/host_tests_ping_stack_stubs_20260909.log`。host試験を実RF成功の代用にはしていない。

## 人間操作と30分枠

最初の枠は2026-09-08 **23:11〜23:41 JST**。USB確認、sudo認証の一括依頼、ESP build/flash、guarded SX buildを優先したが、SXの起動・手動書込みまでをこの枠内では完了できなかった。

| 時期 | 実際に必要だった人間操作 |
|---|---|
| 最初の30分 | アンテナ/GND/SW1の報告、ESPから3.3V電源線を追加していないことの確認。23:40:25に特権setup scriptを実行 |
| 30分以降〜09日朝 | SW1 SA/Download切替、JP2/CN8設定とLEDの確認、通信可能USBケーブルへの交換、必要なSX電源再投入。ROM/RAM読取り停止後の再起動、GUI書込み失敗の報告 |
| 09日08:39 | 特権setup再実行と最初のGUI書込み試行。2ND bootloader ping失敗は書込み成功に数えていない |
| 09日18:46〜18:56 | 固有SXポートと画像を固定したGUIでStandalone書込みComplete、SAへ切替。RF確認後にcustom書込みComplete、SAへ切替。18:47:28/18:55:09に特権setup再実行 |
| custom起動確認以降 | HSPI/EIRQ/loopback、association、ping、UDP、映像、GUI確認、ESP build/flash/resetとmain再検証はCodex側で実施。追加の人間操作は記録されていない |

SXへのflash STARTとSW1/電源操作は人間が実施した。最初のsudo認証が完了しなかったため特権処理をscriptへまとめ、後の再実行もSX作業と同時に依頼した。全時系列・保留理由・誤った仮説の撤回は [セッション履歴](docs/STAGE1_SESSION_HISTORY_20260909.md) に保存している。

一時受信許可はenp0s31f6のUDP50000とICMP echoに限定。複数の解除timerが残るため、今回の有効期限は早い **2026-09-09 22:47:28 JST** を基準にする。期限後に再試験する場合は既存 `tools/prepare_privileged.sh` の再実行が必要であり、恒久NixOS設定変更は行っていない。

## 再現手順と証跡

GUIの受信をStopしてUDP50000を解放する。SXには検証済みcustomを保持したまま、ESPのflash以降を再実行する。SXへの再flashが必要な場合は人間が行う。

```sh
cd /home/hotaru/silex/halow_stage1_main
export STAGE1_WIRING_CONFIRMED=1 STAGE1_RF_CONFIRMED=1
export ESP_IP=192.168.10.20 PC_IP=192.168.10.250 UDP_PORT=50000
TEST_NETWORK=1 UDP_SECONDS=30 LOG_DIR="$PWD/logs/recheck" ./tools/test_stage1.sh --eirq
```

全負荷を選ぶ上記commandは1 Mbpsの配送条件に届かなければ非zeroで終了する。現在の実測では終了値1となり、同時にTEST_VIDEOを指定しても動画phaseへ進まない。360pは次の別commandで結果を残す。映像partitionが未準備の場合だけ `FLASH_VIDEO=1 ./tools/flash_esp.sh` でHVC1もESPへ書込み、実partitionとreadbackを検証する。

```sh
VIDEO_CLIP="$PWD/assets/video.bin" VIDEO_SECONDS=60 \
LOG_DIR="$PWD/logs/recheck-video" ./tools/test_video.sh
```

再現に必要な `hw_inventory.sh`、`find_usb.sh`、`build_esp.sh`、`flash_esp.sh`、`monitor_esp.sh`、`test_hspi.sh`、`test_udp.sh`、`test_video.sh`、`collect_logs.sh`、`test_stage1.sh` をtools配下に保存した。

作業branch: `codex/stage1-real-hw-20260908-231300`。
worktree: `/home/hotaru/silex/_codex_work/worktrees/stage1-real-hw-20260908-231300`。
提供directoryはGit repositoryではなかったため、元の内容を保持して初期main commit `69115b3` を作成した。既存fileの削除・stash・resetはしていない。

最終evidence archiveの出力先:
`/home/hotaru/silex/_codex_work/evidence/stage1-real-hw-final-20260909.tar.gz`

原試験/main試験のH.264、GUI screenshot、runtime log、main build manifest、本報告を収集する。SHA256は同じdirectoryの `stage1-real-hw-final-20260909.tar.gz.sha256` に記録し、内容と照合する。既存 `logs/stage1-fixed-20260909.tar.gz` は動画前に作成されたもので、最終archiveとしては扱わない。生成binaryとprivateなfactory backup/FlashTool生logはGit外で管理し、privateな工場backupとFlashTool生logは最終archiveにも含めない。
