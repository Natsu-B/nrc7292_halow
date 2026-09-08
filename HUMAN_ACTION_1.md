# HUMAN ACTION REQUIRED #1

想定拘束時間: 10～20分。開始時刻 2026-09-08 23:11 JST、最初の30分枠は23:41まで。

## A. 一度だけ確認

- SX/APアンテナ接続、ESP/SX GND共通は人間確認済み。再確認不要。
- ESPの3.3V電源線はSXに接続不要。残る確認はSX-EVK host I/Oの電圧設定（出荷時から変更していないか）。
- SW1現在値は人間確認済み `L H L L L H`（Hosted）。
- APの既知IP、SSID、security（分かる範囲）。
- 特権処理は `tools/prepare_privileged.sh` を通常ユーザーの端末で実行。sudoはこのscriptが1回だけ要求する。

Scriptはenp0s31f6限定90秒のpcap、UDP50000/ICMP echoの一時受信許可を設定。
許可は4時間後またはrebootで解除。恒久NixOS設定変更なし。

現状: SXのUSB-UARTは識別済みだが115200bpsでは受信0 byte。既存FWの種類は未判定。
SXの電源OFF→SA `L L L H L H`→電源ONのboot logを確認する。
既存Standaloneが起動すれば再書込み不要。

## B. Standaloneを書込む必要がある場合

SXへの書込み操作は人間が行う。ESP側はCodexが診断FWでSPI未開始にする。

1. SX電源OFF。
2. SW1 Download: `L H H L L H`。
3. デスクトップ端末で `tools/open_sx_flashtool.sh` を実行（Java GUI、sudo/Wine不要）。同梱 `_README_LINUX` の `java -jar` 方式。
4. 2ND BOOTLOADER: `/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool/firmware/nrc7292_boot.bin`
5. XIP BOOTLOADER: `/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool/firmware/nrc7292_boot_xip.bin`
6. XIP FIRMWARE: `/home/hotaru/silex/nrc7292_xip_fw_sx_uart.bin`
7. MAC ADDRESSのWRITEはOFF。工場MAC/BDFを変更しない。
8. 通常のSilex手順で電源ON・書込み。完了後SX電源OFF。
9. SW1 SA: `L L L H L H`。SX電源ON。
10. `standalone ready` と入力。

Standalone firmware: 855036 bytes

SHA256: `7e93f02fb67a0d6504998a7372dc01cdcaf47c800c70f5b8224844dc85170ccf`

2ND bootloader SHA256: `33db5706c1be366c07908eb0093ad5a6c1254778149415bf16971bc12c9c4057`

XIP bootloader SHA256: `51da4440d881fd77fdb91c11dcc8d19d81ffbfb76f68c2eaff07fc6826886be4`

Bootloader指定はSilex提供Standalone Start-Up Guideの指定に照合済み。
CN8 console jumpers 5–6、7–8も同ガイド記載。USB-UARTブリッジの認識だけではUART配線の疎通は証明されない。

無線のscan/association/IP試験はCodexが行い、JP Duty/LBTは維持する。
