# Stage-1 実機結果

更新: 2026-09-09。SA、ジャンパー配置、CN2 USB単独給電がメーカー対応構成であることを確認済み。FlashToolの自動migrationを調査中のためSX書込みは保留。

| 項目 | 結果 | 実測・根拠 |
|---|---|---|
| USB ESP32-S3 | PASS | 303a:1001、serial/MAC 90:70:69:10:C7:B4、esptoolでESP32-S3 rev0.2 |
| USB SX-NEWAH | PASS | 10c4:ea60、CP2102N serial 08db3e3b2974ea11adf500fb6a30b0f3。Download modeでROM PONG 16 bytes、RAM bootloader応答も確認 |
| Ethernet AP-100AH | PASS | ARPから192.168.10.10 / 1c:bc:ec:1b:30:feを検出、HTTP200、画面にAP-100AHと表示 |
| SX standalone boot | NOT_CONFIRMED | SA/CN8/USBケーブル確認後の06:22電源再接続を捕捉。115200 8N1のboot出力と改行/help/sf sizeは受信0 byte。現行FW未確定 |
| SX → AP association | NOT_TESTED | Standalone boot未確認 |
| AP → PC Ethernet | PASS | 実IP192.168.10.10へのping3/3、0% loss。候補IP失敗はアドレス違いだった |
| custom SX build | PASS | native HSPI、832972 bytes、guarded bin。ARM実link済み |
| custom SX flash | NOT_DONE | 人間専用操作、依頼手順準備済み |
| ESP build | PASS | ESP-IDF v5.5.2 / esp32s3、最終339536 bytes |
| ESP flash | PASS | by-idをesptool再照合、書込み後hash一致 |
| ESP boot | PASS | 実boot log、GPIO表示、console応答 |
| CE0 GPIO6 probe | NOT_TESTED | GNDとI/Oジャンパー配置確認済み。SXのFW準備を優先、ESPはSPI未開始 |
| CE1 GPIO5 probe | NOT_TESTED | 同上。両CS同時Low禁止の切替はhost regressionで検証 |
| HSPI 1MHz | NOT_TESTED | 5MHz両候補失敗時のfallback実装済み |
| HSPI 5MHz | NOT_TESTED | 電気試験未開始 |
| EIRQ GPIO4 | NOT_TESTED | input/high-level実装、polling後有効化。実信号未測定 |
| loopback 1518×1000 | NOT_TESTED | SX custom未書込み |
| AP association custom FW | NOT_TESTED | SX custom未書込み |
| IPv4 ping | NOT_TESTED | 無線経路未確立 |
| UDP 100k | NOT_TESTED | 無線経路未確立 |
| 360p | NOT_TESTED | 下位層未確立 |
| merged to main | NO | 必須HSPI probe PASS未達 |
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

現在実flash済ESP bin SHA256（ping/UDP追加、再probeキャッシュ誤PASS修正）:
`6eb638c0d5d1bb7fad391a0d1de935bf943772a52e6e1d9c805f05f285250e0d`

`logs/esp_flash_reprobe.log`の3領域hash照合と`logs/esp_boot_reprobe.log`で最終imageを確認。
ping/UDPの未接続時拒否は実機で確認済み。疎通成功としては数えない。

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

現在のPC有線IPは `192.168.10.250/32`。APへの `192.168.10.10/32` だけを有線routeに設定。
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
- `logs/privileged/`: 人間が実行した特権setupと実Ethernet pcap。自動解除timerは03:40:25 JSTに設定済み。
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
