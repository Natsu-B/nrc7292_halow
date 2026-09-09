# Stage-1 実機結果

更新: 2026-09-09 19:30 JST。ESP ping taskのstack overflow修正後、HSPI/EIRQ/loopback/IP再検証PASS。UDP100/250/500kbps損失0、360p連続120frame byte一致、Electronで実H.264描画を確認。1MbpsはDuty停止とSX queue dropを伴い未達。mainへのmergeとmainからの再検証へ進む。

| 項目 | 結果 | 実測・根拠 |
|---|---|---|
| USB ESP32-S3 | PASS | 303a:1001、serial/MAC 90:70:69:10:C7:B4、esptoolでESP32-S3 rev0.2 |
| USB SX-NEWAH | PASS | 10c4:ea60、CP2102N serial 08db3e3b2974ea11adf500fb6a30b0f3。Download modeでROM PONG 16 bytes、RAM bootloader応答も確認 |
| Ethernet AP-100AH | PASS | ARPから192.168.10.10 / 1c:bc:ec:1b:30:feを検出、HTTP200、画面にAP-100AHと表示 |
| SX standalone boot | PASS | 指定bin書込み後、nrc7292_standalone_xip CLI/help/sf size応答、1MiB確認 |
| SX standalone flash | PASS | 18:46:59にGUI Complete。指定3画像のhash一致、MAC/RF migration skipped |
| SX → AP association | PASS | halow_demo_video、BSSID 1c:bc:ec:1b:30:ff、Open、wpa_state=COMPLETED、scan RSSI -10dBm |
| AP → PC Ethernet | PASS | 実IP192.168.10.10へのping3/3、0% loss。候補IP失敗はアドレス違いだった |
| Standalone SX ↔ PC ping | PASS | SX10.20→PC10.250とPC→SX各5/5。PC送信はenp0s31f6に固定 |
| custom SX build | PASS | native HSPI、832972 bytes、guarded bin。ARM実link済み |
| custom SX flash | PASS | 18:54:41にGUI Complete。指定guarded bin、MAC/RF migration skipped。custom boot/HSPIは別判定 |
| custom SX boot | PASS | native HSPI応答、期待するSW/board ID、HELLOおよびloopbackでアプリ動作確認 |
| ESP build | PASS | ESP-IDF v5.5.2 / esp32s3、実測AP/Open/IPおよびping stack修正、365456 bytes |
| ESP flash | PASS | by-idをesptool再照合、書込み後hash一致 |
| ESP boot | PASS | 実boot log、GPIO表示、console応答 |
| CE0 GPIO6 probe | PASS | 5MHz、chip0x7292/modem1/sw0x800/board0x07020902/NRC-HSPI、RX/TX各32×512 |
| CE1 GPIO5 probe | NOT_TESTED | 第一候補CE0が成功したためfallback未実施、CE1はHighを維持 |
| HSPI 1MHz | NOT_TESTED | 5MHz両候補失敗時のfallback実装済み |
| HSPI 5MHz | PASS | probe、HELLO、polling/EIRQ loopback実測 |
| EIRQ GPIO4 | PASS | active-high level、IRQとHigh検出count各1370、loopbackエラー0 |
| loopback 1518×1000 | PASS | polling 15865ms、EIRQ12068ms。両方1000/1000、corruption/sequence/SPI error0 |
| AP association custom FW | PASS | SDK blocking connect成功、link=1、RSSI-10、Open認証 |
| IPv4 ping | PASS | 修正後ESP→PC/PC→ESP各5/5。終了後監視でpanicなし、ping stack最小空き2228 bytes |
| UDP 100k | PASS | 修正版30秒、367送信/367受信、375808 bytes一致、loss/corruption/send_error/Duty停止0、安定性PASS |
| UDP 250k | PASS | 30秒916/916、loss/corruption0、Duty停止0 |
| UDP 500k | PASS | 30秒1832/1832、loss/corruption0、Duty停止0 |
| UDP 1M | FAIL | 2125送信/1986受信、139 loss=SX queue drop139。Duty停止12574ms、実効542310bps |
| 360p | PASS | 640×360、同一stream連続120frame byte一致、60秒597frame一致、破損/失効0 |
| Electron GUI | PASS | 実UDPからH.264をWebCodecsで復号・描画、表示440→465frame。Electron40.10.3 |
| merged to main | NO | 通信・安定性・360p確認済み。これからmergeしmain再build/reflashを実施 |
| merge commit | — | merge未実施 |

NOT_TESTEDは未試験を表し、測定した故障とは区別する。Standaloneの無線成功とSPI成功も別々に判定する。

## 実機識別

ESP: `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_90:70:69:10:C7:B4-if00`

SX: `/dev/serial/by-id/usb-Silicon_Labs_CP2102N_USB_to_UART_Bridge_Controller_08db3e3b2974ea11adf500fb6a30b0f3-if00-port0`

ESP実Flashは8MiB、現在のStage1 partition/headerは4MiBに制限した既存構成。
eFuseの読取りではquad Flash / 3.3V。これはSX host I/O電圧の確認を代替しない。

実配線: SCLK9 / MOSI7 / MISO8 / EIRQ4 / CE0=6 / CE1=5、RST未接続。
ESP書込み前のFWはSD/ICMセンサー用だった。診断FWはSPI/RFを起動時に開始しない。

## Firmware

最初の実flash済ESP bin SHA256:
`f254fa6a87cfbb8f9c5866825127ece6d039b0dc6a1387d1c3977b6db3a36382`

以前の実flash済ESP bin SHA256（ping/UDP追加、再probeキャッシュ誤PASS修正）:
`6eb638c0d5d1bb7fad391a0d1de935bf943772a52e6e1d9c805f05f285250e0d`

`logs/esp_flash_reprobe.log`の3領域hash照合と`logs/esp_boot_reprobe.log`で最終imageを確認。
ping/UDPの未接続時拒否は実機で確認済み。疎通成功としては数えない。

以前の実flash済ESP bin SHA256（実測AP/Open/IPを反映）:
`071a90db9de8999553070a3c486027a3e67027e54fc4d56c3a504f34b0c60cb4`

365248 bytes。`logs/esp_flash_observed_ap_20260909.log`でESP32-S3個体と3領域hash照合を確認。
build後のcommitによりflash時にproject versionが更新されたため、build直後のhashではなく実flash完了後のbin hashを記録。
`logs/esp_pins_observed_ap_20260909.log` と `logs/esp_diag_observed_ap_20260909.log` でアプリ応答・実配線表示・SPI未開始を確認。

現在実flash済ESP bin SHA256（ping task stack修正、SNR表示追加）:
`38b4c37c0d88d4afd53157539eee5b9b197b64d9e34e8e7b093f9947a9c263da`

365456 bytes。`logs/esp_flash_ping_stack_video_20260909.log` で照合。映像partitionへ既存640×360 HVC1 clipも書込み、hash確認済み。

SX custom候補:
`/home/hotaru/silex/_codex_work/prepared_sx_20260908_guarded/out/nrc7292/standalone_xip/L2BRIDGE_HSPI/nrc7292_standalone_xip_L2BRIDGE_HSPI_2M.bin`

SHA256: `c05ec7b2bdf3dc94ccba87ac8d543bdd2db1cc4e5ee7ae770df2f57d89fe6a01`

Silex UART Standalone: `/home/hotaru/silex/nrc7292_xip_fw_sx_uart.bin`

SHA256: `7e93f02fb67a0d6504998a7372dc01cdcaf47c800c70f5b8224844dc85170ccf`

SDKにはFlashマップの自動移行処理があり、RF_CAL/SYS_CONFIGを書き込むことを逆アセンブルで確認。
guarded binは当該移行処理を抑止し、最終ARM呼び出し先と戻り値を確認済み。
JP Duty/LBTは有効。提供BDFは無変更。実機でのboot互換性は未確認。

## Git

提供ディレクトリはGit repositoryではなかったため、内容を保持してmain初期commit `69115b3`を作成。
既存ファイルを削除・stash・resetしていない。

開発branch: `codex/stage1-real-hw-20260908-231300`

worktree: `/home/hotaru/silex/_codex_work/worktrees/stage1-real-hw-20260908-231300`

mainへmergeしていない。mainからの再build/reflashも未実施。

## 最初の30分と人間操作

開始23:11 JST、30分枠は23:41まで。

- 23:11 USB/Ethernet inventory、sudo -v開始。
- 23:13 初回質問でアンテナ、GND、3.3V、SW1、sudo認証を一括依頼。
- serialは既存dialout権限で利用可能。NetworkManagerの既存権限で一時IP設定可能。
- 最初のsudo対話は認証未完了で終了。ユーザー指示に従い特権処理をshell script化。
- 23:20 SX build初回成功。自動Flash移行が見つかり、この初回binは書込み対象から除外。
- 23:23 ESP実flash成功、23:25 bootとpins確認。
- 23:27 guarded SX bin完成、書込み手順準備。
- 人間が実際に行った操作: アンテナ/GND/3.3V電源線未接続/SW1 Hostedの報告。
- 23:40:25 人間が `tools/prepare_privileged.sh` を実行。kernel module、runtime firewall、90秒pcapを記録。
- UDP50000とICMP echoのenp0s31f6限定一時受信許可は4時間後自動解除。恒久設定変更なし。
- 23:41 pcapに実APのgratuitous ARP。23:42 AP実IPへ有線疎通成功。
- 30分以降の人間操作: SX SAへの手動切替/起動を待っている。FW完成待ちでの拘束はない。

現在のPC有線IPは `192.168.10.250/32`。APへの `192.168.10.10/32` と後続SX試験用 `192.168.10.20/32` を有線routeに設定。
Wi-Fiの `192.168.10.136/24` と競合させず、インターネット経路はWi-Fiのまま。

AP公開画面: firmware `BB3-5.10 (2024.06.14)`、SSID `halow_demo_video`、925.5MHz/2MHz、BSSID `1c:bc:ec:1b:30:ff`、接続STA 0台。
認証方式は設定画面のログインが必要で未取得。表示MCS7/-95dBmは接続STAゼロ時の値なのでSXの測定結果とは扱わない。
ユーザーはAPリセットも許可。現時点では実施していない。

開発commit: `fe708d1` 実GPIO、`bccdc8b` native診断/IP試験、`5fef123` SX自動移行抑止、`c4d297b` 実機自動化。
host検証: 12 C tests、12 receiver tests、実parserを使うC→Node互換、CE切替/再probe切断、UDP/Duty分離、映像cycle/fragment並替え、USB個体識別、SX USB再接続を検証。
これらhost試験は実HSPI/RFの成功を代替しない。

## ログと次の操作

- `logs/hardware_inventory.txt`: 全inventoryとudev identity。
- `logs/esp_chip_id.log`, `logs/esp_flash.log`, `logs/esp_boot.log`, `logs/esp_pins.log`: ESP実機証跡。
- `logs/esp_probe_before_confirmation.log`: 未確認時のprobe拒否、SPI未開始。
- `logs/sx_initial_console.log`: SX受信0 byteの試行。
- `logs/ap_probe.log`: AP候補IPへの有線試行。
- `logs/ap_status.json`: AP実機HTTPから取得した認証情報を含まない設定/状態。
- `logs/privileged/`: 人間が実行した特権setupと実Ethernet pcap。18:47:28および18:55:09に再実行を記録。18:47の解除timerも残るため、受信許可の満了は早い方の22:47:28 JSTを基準にする。
- `HUMAN_ACTION_1.md`: 最初の確認、Standalone起動/必要時書込み。
- `HUMAN_ACTION_2.md`: RF独立確認後のcustom書込み。

初回確認→SX Standalone boot→scan/association→custom SX書込み→ESP polling probeの順で再開する。

SW1はSAへ変更済み。JP2/CN8のユーザー確認も完了。CN2 USB単独給電はメーカー対応構成であり、CN1追加は不要。LED状態と既存FW起動を確認し、書込みが必要な場合は下記migration問題の解決後に人間へ依頼する。
GND/アンテナは確認済みなので再確認しない。ESPの3.3V電源線は接続不要。
書込みが必要になった場合だけ `tools/open_sx_flashtool.sh` をデスクトップ端末から実行する。
同梱Linux説明書に従うJava GUI方式で、Codex自身はSXへflashしない。

## 2026-09-09 再開

- ユーザーがSW1変更を報告。SXの同一by-idは05:43に再enumerate。GND/アンテナ確認は再要求していない。
- I/O設定は「変更していないはずだが自信なし」、テスターなしと回答。
- ESPの実 `diag` は `opened:false`。SPI出力はまだ開始していない。
- SX UART再試行は受信0 byte（`logs/sx_sa_20260909.log`）。UART未接続とFW未起動をまだ区別できない。
- Silex提供Standaloneガイドp6はCN8の5–6/7–8がコンソールに必須と明記。ユーザーへ装着状態を問い合わせ。
- I/O用JP2は公式電力測定資料の2–3接続が3.3V試験構成。提供JP基板写真の同位置にも照合。ただしUS/JPのPCB型番が異なるため、実基板写真でJP2/CN8を照合してから判断する。
- MISO浮遊/EIRQ LowのADC値では電源電圧は判定できないため、そのような推定は使用しない。
- AP `192.168.10.10` への有線pingは2/2で再成功。AP設定変更/resetなし。
- 前回の一時受信許可timerは03:40に満了。新たな特権変更は行っていない。

参考: [Silex Power Measurement](https://www.silextechnology.com/hubfs/Resource%20PDF/142-20159-120%20SX-NEWAH%20Power_Measurement.pdf)、提供 `WA107660XA` p6、提供JP Startupガイドの基板写真。

## 2026-09-09 05:55以降のジャンパー確認

- ユーザー回答: CN8の5–6/7–8は両方装着、JP2はSilexロゴを正立させた向きで右側に装着。
- JP基板写真の右側はSW1寄り/「3」側。公式電力測定資料のJP2 2–3・I/O3.3V構成に一致するため、3.3V設定位置として確認済みに変更。電圧の実測値を得たとはしない。
- 追加写真やテスターは要求しない。
- `logs/sx_confirmed_jumpers.log`: 確認後もUART受信0 byte。
- `logs/esp_before_sx_flash.log`: `diag`は`opened:false`。書込みに備えてSPI未開始を維持。
- SX Standalone binとSilex bootloader2個のSHA256を再確認。CodexはSXへflashせず、人間用手順を更新。

## CN2単独給電についての確認

- ユーザーから、SXはUSB CN2のみ接続、CN1追加が必要かとの質問。
- 提供Silex StandaloneガイドWA107660XA p13はUSBまたはACアダプタによる給電を明記し、USB接続・CN1未接続の写真を掲載。JP StartupガイドWA107280XD p8もCN2をUSB power Connectorと記載し、USB給電可能と明記。
- CN1の追加接続は不要。ESPから3.3Vを追加配線する必要もない。
- Codexが一時的に提示した「CN2ではUSB-UARTだけに給電される可能性」という仮説は、この資料確認により撤回。CN2の接続方式だけを無応答の原因とはしない。
- SX本体の起動は依然未確認。USB-UART認識とSX MCUのboot成功を分けて記録する。

## FlashTool書込みを保留した理由

- 提供FirmwareFlashToolのJava bytecodeに、MAC WRITEチェックより前に`cmd_sf_migration()`を呼ぶ経路を確認。
- 同処理はコマンド0x44を送信し0x45応答を期待し、MAC address/RF cal migration done/skippedの分岐を持つ。MAC WRITE OFFだけで自動移行も停止するとはいえない。
- 実際に移行される条件と提供2ND bootloader側の処理は調査中。工場MAC/BDFを勝手に変更しない指示を守るため、SXへの書込み依頼を保留。ツール起動/書込みは未実施。
- これはcustom SX firmware内に実装済みの起動時migration抑止とは別の、PC書込みツール側の処理。
- 調査結果: `docs/SX_FLASHTOOL_FACTORY_WRITE_AUDIT_20260909.md`。提供2ND bootloaderの条件付きMAC/RF_CAL書込みへ到達することを確認。通常/acceleratedとも同処理を呼び、GUIの既存設定での回避経路は未発見。

## LEDとUSBケーブルの追加確認

- ユーザー観察: 基板の下から1番目・4番目のLEDが点灯。提供資料に個々のLED機能表がないため、特定の電源railやNRC firmware起動成功へ結び付けない。
- ユーザーが通信可能と確認できるUSBケーブルへ交換。SX CP2102Nの同じUSB serial/by-idを認識しており、デバイスの再列挙時刻は06:10。
- `logs/sx_led_confirmed_20260909.log`: UART 115200 8N1、改行/help/sf sizeに受信0 byte。
- 起動直後の出力を捕捉するため同一by-idの受信待機を開始し、SW1 SAのままCN2抜き差し1回を人間へ依頼。FlashToolは未起動、ESP SPIも未開始。
- ユーザーが再接続を実施。`logs/sx_power_boot_20260909.log`には切断と06:22:10の同一by-id再openを記録。UART受信データは0 byte。USB再列挙前の最初期出力は捕捉できない場合があるため、ログ不在だけで故障と断定しない。
- `logs/sx_after_power_cycle_20260909.log`: 再接続後の改行/help/sf sizeにも受信0 byte。モニターは停止してポートを解放。
- 次の切り分けはROM PING。通常のFlashTool STARTは使わず、工場arrayを書かないRAM読取り経路も準備する。実行前に送信command/addressを静的・offlineで確認する。
- `logs/sx_rom_probe_sa_20260909.log`: SA設定で115200 bpsのROM PINGは受信0 byte/timeout。RAM load/flash commandとも0。SAでROM応答がないことはROM故障の証明ではない。
- PING専用ツールを準備した後、SW1 Download `L H H L L H`への切替とCN2再接続を人間へ依頼。通常FlashToolのSTARTは引き続き保留。

## 2026-09-09 07:42以降のROM/RAM実機確認

- 人間がDownload設定で再起動し、`download ready` と回答。
- `logs/sx_rom_probe_download_20260909.log`: ROM PING PASS。115200 bps、PONG 16 bytes。これによりPC→USB-UART→NRC本体の通信を確認。StandaloneやHSPIの成功ではない。
- `logs/sx_ram_info_20260909.log`: 固定SHAのSilex 2ND bootloaderをRAMへロード・実行し、460800 bpsのRAM map/geometry読取りPASS。元のRAM mapも復元一致。SX flashへのprogram/erase/migration要求は送信していない。
- loaderは1MiBを報告したが、未知JEDEC時の既定値である可能性がある。mapには容量外のFOTA/DEVICE_INFO領域もあり、物理flash容量1MiBとはまだ断定しない。
- `logs/sx_factory_backup_20260909.log`: 同一起動から限定領域の二重読取りを開始したが、追加target CRC要求0x95の段階でtimeout。最初のsector読取り相当の応答は得られたものの、CRC前に保存していなかったためfactory sectorのバックアップは未成立。RAM map復元も未確認のため通信を停止。
- 0x95 handlerは非整列RX payloadから32bit LDRする。停止状況と整合する有力原因だが、fault PCは取得していないため原因確定とはしない。先行の静的監査はNV書込みの不在を確認したものであり、実行安全性PASSではなかった。0x95をツールの許可対象から除外して再使用しない。
- 修正版は二重読取り一致の時点で0600のファイルを保存し、host SHA256を記録する。機器側CRCをPASSと扱わない。修正・offline検証後にDownload設定のまま電源再投入を人間へ依頼する。

## 2026-09-09 08:15以降の取得と書込み判断

- 人間がDownload設定のまま電源再投入（返答 `ok`）。`logs/sx_rom_probe_reboot_20260909.log`でROM PING再PASS。
- `logs/sx_factory_backup_retry_20260909.log`: `acquisition_result=PASS`。map `0xf000`、SYS_CONFIG `0xfe000`、RF_CAL `0xff000` の各4096 bytesを2回ずつ読み、byte一致。RAM mapも復元一致。機器側CRCはNOT_TESTEDなのでmanifest全体はPARTIALという保守的表記。未取得の容量外DEVICE_INFOを含むfull flash backupではない。
- 保存先 `/home/hotaru/silex/_codex_work/private_sx_backups/20260909-081543-554105/`。ファイル0600、Git外。SYS/RFの内部nrct record CRCも別途hostで照合一致。RF countryはJP。
- 固定RAM stack metadataからJEDEC `0xef6014` を取得。loader報告容量は1MiB。SPI Flashの電圧型番からESP/SX host I/O電圧を推定しない。
- `logs/sx_legacy_marker_20260909.log`: GUIが実際に参照するliteral offset `0xffffe000` の44bytesを読み、marker `nrcnrt` 不在、RAM map復元一致。物理SYS先頭と同一ではなかったため、アドレスaliasの推測は採用しない。MAC移行条件は実参照先でも不成立。
- RF移行条件も不成立。既存SYS versionは2で、0x46再保存の再構成はCRCを含め44bytes全部が同一、残りsectorもFF。通常GUIのMAC0/MAC1 WRITE OFFで論理工場MAC・JP校正値を保持できる条件を確認した。工場領域へ一切erase/programが無いと主張するものではない。
- XIP firmware/bootloaderの書込み範囲はSYS/RFと重ならず、消去対象に含まれる `0xf000` は既に全FF。FW_INFO `0xf5000` の更新はfirmware管理情報。
- Standalone書込み保留を解除し、人間用HUMAN_ACTION_1を更新。Codex自身によるSX flashは未実施。
- 後続IP試験用に `192.168.10.20/32` の有線host routeをNetworkManager一時profileへ追加。`ip route get`はenp0s31f6/source192.168.10.250。Wi-Fi default routeは維持。
- 期限切れのネットワーク受信許可は、既存 `tools/prepare_privileged.sh` の再実行を人間作業へまとめた（まだ再実行完了とは記録しない）。

## 2026-09-09 08:39以降のGUI書込み失敗

- 人間が08:39:30に特権scriptを再実行。`logs/privileged/setup-20260909-083930.log`で一時firewallと4時間後の解除timer設定を確認。sudoの追加依頼は不要。
- 人間から「pinging 2nd bootloader failedとなってGUIが止まった」と報告。このメッセージは通常uploadXIP内で、移行0x44・version更新0x46・flash erase/programより前に出る。今回の失敗をStandalone flash成功と数えない。
- GUI終了後、`logs/sx_ping_rates_after_gui_20260909.log`: 460800 PINGは受信0 byte、115200 PINGはPONG 16 bytes。2nd loaderの起動未確認だが、ROM通信は正常。RAM転送が実行されたかはGUIログ未取得のため未確定。
- `logs/sx_vendor_java_ping_20260909.log`: 配布GUIそのもののNewrafc.cmd_ping（200ms timeout）をidentified SX by-idへ実行し、6msでPONG 16 bytes、PASS。PINGのみ、RAM/flash/RUN要求は0。常時発生するJava通信不能やtimeout不足とは断定できない。
- 静的確認: GUIのRefreshは一覧の最後のポートを自動選択し、以前の選択を保持しない。別接続のESPを選ぶ可能性があるが、今回ユーザーが実際に選んだポートは未取得。原因を誤選択と確定しない。
- GUIには部分送信の戻り値未確認と65536 zero bytesを送るflushもあるが、今回の原因とは未確定。通信コードの変更は行わず、まずSXポートと指定firmwareの自動選択、GUIログ保存を用意する。
- `logs/sx_gui_virtual_check_20260909.log`: 同じPC上の仮想displayでGUIを構築すると、元の初期portが`ttyACM0`（ESP）だった。launcher適用後はSXの固有by-id、Silex指定2ND/XIP bootloader、指定Standalone firmware、MAC0/MAC1 WRITE OFFに一致。STARTは押さず、設定確認後10秒のtimeoutでGUIを終了した。flash/serial通信は未開始。
- `tools/open_sx_flashtool.sh` と `tools/SxFlashToolLauncher.java` は配布GUIの通信・flash処理を変更せず、初期設定と人間START時の照合を行う。GUI binaryはSHA256固定。通信ログは0600で保存しGit追跡から除外。実SX書込み成功を示す変更ではなく、次の人間操作を正しいポートで再現する準備。
- 最終版の `--check` と仮想displayの `--gui-check` はPASS。`logs/sx_gui_prefill_check_20260909.log` に実GUI構築・設定一致、START=0、serial_open=0を記録。START直前にmodule/port/3画像のpathとhash/460800/XIP Upload/MAC WRITE OFFを照合する。mainへのmerge条件は引き続き未達。

## 2026-09-09 18:48以降のStandalone RF実証

- 人間が18:46に修正版launcherからStandaloneを書込み。指定の3画像、NRC7292、SX by-id、MAC0/MAC1 WRITE OFFで実行し、GUI Completeまで進行。MAC/RF移行は実際にもskip。機密を含まない要約は `logs/sx_standalone_flash_summary_20260909.txt`。
- 人間が18:47:28に特権scriptを再実行し、SA起動後 `ready` と返答。新たなアンテナ/GND/電圧確認は要求していない。
- `logs/sx_standalone_ready_20260909.log`: CLI応答、sf size=1048576、scanでSSID halow_demo_video/BSSID 1c:bc:ec:1b:30:ff/RSSI -10を取得。
- `logs/sx_standalone_link_20260909.log`: wpa_state=COMPLETED、key_mgmt=NONE、pairwise/group cipher=NONE。Open認証で接続済み。現在のSX IPは192.168.10.20/24で、SSID/auth/IP設定変更や追加resetなしで試験できた。wpa表示freq=5190はそのまま記録し、APの実RF表示925.5MHzとは区別する。
- `logs/sx_standalone_ping_20260909.log`: SX→PC192.168.10.250、5送信/5受信/0% loss、平均5.19ms。
- `logs/pc_to_sx_standalone_ping_20260909.log`: PC/enp0s31f6→SX192.168.10.20、5/5、平均21.756ms。`logs/pc_to_ap_ping_20260909.log`: AP10.10へ3/3。
- `logs/sx_standalone_factory_map_20260909.log`: SYS0xfe000/RF0xff000/FW_INFO0xf5000、工場MAC読取り。FOTAなど容量外のplaceholder表示を実容量とは扱わない。
- `SX_STANDALONE_RF=PASS`、`AP100AH_ETHERNET=PASS`。これはHSPI実装の成功ではなく、native HSPI/ESP IP/UDP/映像はまだ未試験。
- 18:52、同じlauncherへguarded custom binを引数指定するHUMAN_ACTION_2を依頼。ESPのネットワーク既定値も実測SSID/Open/10.20/24/PC10.250に合わせてbuild中。JP Duty/LBTは変更しない。
- `logs/sx_flashtool_20260909-185400-1953056.log`（private）: 人間がguarded custom binを選択したlauncherでSTARTし、18:54:41にComplete。MAC/RF migration skipped。CodexからSXへのflash要求は送っていない。USBは18:56:13に再列挙、SA起動完了の返答待ち。
- ESP Kconfig実測設定変更はcommit `0285127`。SSID halow_demo_video、OPEN、ESP192.168.10.20/24、PC192.168.10.250、gateway0.0.0.0。同一subnetへの試験にdefault gatewayは不要。build/flash/pins/diagはPASS、SPIはまだ開始していない。

## 2026-09-09 native HSPIとcustom無線/IPの実証

- `logs/esp_boot_same_fd_20260909.log`: ESP専用HardReset後、同じserial fdで全bootを取得。app version0285127-dirty、指定pins、boot時SPI/RF未開始のbannerを確認。SXはresetしていない。
- custom書込みCompleteとUSB再接続/給電を認識後、chip/signature読取りを実行。`logs/hspi_initial_probe_20260909.log`: CE0/GPIO6 5MHz PASS。期待するNRC7292 IDとnative queue geometryを取得。CE1/1MHz fallbackは不要だった。
- `logs/hspi_polling_loopback_20260909.log`: HELLO PASS、64×100/512×100/1518×1000すべてerror0。最終TX slots4301/RX4475で、cache表示だけでなく実転送を確認。
- `logs/hspi_eirq_loopback_20260909.log`: polling成功後にEIRQ有効化。同じ3種loopbackすべてPASS、IRQとHigh検出counter各1370。SPI/ACK/header/sequence/status errorsはいずれも0。custom起動をソフトウェアで確認できたため、人間の完了入力は不要と伝えた。
- `logs/esp_custom_association_20260909.log` / `logs/esp_custom_status_20260909.log`: ESPから設定/CONNECT、SX側SDKのblocking connectionが成功後にACK。後続link1/RSSI-10、接続直後MCS0/0。MCSは最大速度達成の証拠とはしない。SNRは既存payloadにあるが表示がなかったため、取得成否flag未転送と明記してESP statusへ表示する変更187c058を準備。
- `logs/esp_to_pc_ping_custom_20260909.log` / `logs/pc_to_esp_ping_custom_20260909.log`: 両方向5/5、loss0。PC側はenp0s31f6に固定。ESPのTTL64とStandaloneのTTL255も区別できた。
- `logs/udp-real-100k-20260909/`: 100kbps/30秒、367/367、375808bytes完全一致、実測100215.47bps、lost/corrupt/duplicate/out-of-order0、Duty blocked0ms。Duty計測はtarget availabilityのsamplingであり、RF packet error rateとは分ける。
- UDP開始前にESPのRebooting残片を観測。`logs/esp_ping_stability_20260909.log` でping完了後も20秒受信を続け、ping task stack overflowとsoftware rebootを再現。polling modeでも発生し、EIRQ起因とは扱わない。通信成功イベントだけで安定性PASSとしない。現在ESP修正と試験側panic検出の追加を進めている。

## ping修正後の再検証と360p表示

- ESP ping taskの既定stack2560 bytesでoverflowを再現し、5120 bytesへ明示設定（b7e7dc0）。非同期CLIは維持し、結果出力後のhigh-watermarkを実測。修正版で最小空き2228 bytes。SDK stubと実config引数を検証する回帰testも追加（a645f47）。
- host側はpanic/stack overflow/abort/Rebootingを検出すると失敗するよう修正（27edcae）。ping・UDP完了後も2秒観測し、UDPの配送結果と安定性結果を分離。`logs/host_tests_ping_stack_stubs_20260909.log` でC12/12、Node12/12、Python/互換/ソース整合性tests全PASS。
- `logs/stage1-fixed-20260909/`: 修正版でCE0/5MHz probe、EIRQ、3種loopback、custom association、双方向pingを再実行しPASS。ping_stack configured5120/min_free2228。後続UDPと動画を含め、明示的panic/rebootは再発していない。
- UDP各30秒の結果は同directoryの `udp_100000.json` / `udp_250000.json` / `udp_500000.json` / `udp_1000000.json`。100/250/500kbpsは全送信packetのbyte一致と安定性PASS。1MbpsではDuty停止12574ms、1538送信機会をDutyで見送り、送信済2125packet中139packetをSX queueでdrop。受信欠落139と一致し、corruption0。制限を解除していない。1Mbps delivery FAILにより通しscript全体のexitは1であり、これを全項目PASSとはしない。
- `logs/video-360p-real-20260909/video_result.json`: 60秒、1379packet、597完成frame。連続する同一streamのframe0..119（120枚）を元HVC1の順序・byteと完全照合。不正/重複/順序異常/失効/reference mismatchは0。
- 受信H.264: `logs/video-360p-real-20260909/received_360p.h264`、263049 bytes、SHA256 `d6167cdd1046e2874bd440ed26026c0886957929a515fa09b6df2cebbf4cdd7c`。Gitには生成binaryを入れず、evidence archiveに含める。
- 動画中55sample: RSSI-12dBm、MCS TX4/6/7・RX7、SNR API値27..35dB。SNR取得成功flagはprotocolにないため `snr_validity=unreported` を併記する。target drop139は先の1Mbps負荷試験からの累積で、動画中に増えていない。
- byte一致確認後、既存Nix Electron40.10.3で実デスクトップにGUIを起動。PC10.250でESP10.20から受信し、H.264 avc1.42c016をWebCodecsで復号。`logs/gui_live_confirmation_20260909.json`で表示440→465frameの増加とdecoder errorなしを確認、`logs/receiver_360p_live_20260909.png`に対象アプリだけを保存。package.json指定44.2.0の配布buildは未試験。
- GUI既定IPを実測値へ更新（8f1bafb）。1080pは未試験。Stage-1の360pまでを確認し、main統合と再検証を次に行う。
