# HUMAN ACTION REQUIRED #1

想定拘束時間: 5～10分。SX Standalone firmwareの書込み操作だけを人間が行う。

状態: 書込みは保留。提供FlashToolの自動Flash migrationが工場データへ与える影響を確認中。以下は確認用手順であり、まだSTARTを押さない。

## A. 確認済みの構成

- SX/APアンテナ接続、ESP/SX GND共通は人間確認済み。再確認不要。
- ESPの3.3V電源線はSXに接続不要。
- CN8の5–6/7–8は装着済み。JP2はロゴ正立時の右側（2–3、3.3V設定位置）。電圧実測ではないが、追加写真やテスターは要求しない。
- SXはCN2のUSBから給電。CN1のACアダプタ接続は不要。提供Standaloneガイドp13にUSBのみの給電例、JP Startupガイドp8にもUSB給電可と明記。
- SW1は当初 `L H L L L H`（Hosted）。2026-09-09、ユーザーがSAへの変更を報告済み。
- APは192.168.10.10、有線疎通済み、SSIDはhalow_demo_video。securityは未取得。
- 特権処理のscriptは実行済み。今回のSX書込みにsudoは不要。

現状: SA/CN8確認後もSXのUSB-UARTは115200bpsで受信0 byte。既存FWの種類は未判定。
CN2単独給電はメーカーが認める構成であり、無応答の原因と断定しない。

## B. Silex Standaloneを書込み、起動を確認

SXへの書込み操作は人間が行う。ESP側はCodexが診断FWでSPI未開始にする。

1. SXのCN2 USBを抜いて電源OFF。
2. SW1 Download: `L H H L L H`。
3. CN2 USBをPCへ接続して電源ON。デスクトップ端末で以下を実行（Java GUI、sudo/Wine不要）。

   ```sh
   /home/hotaru/silex/_codex_work/worktrees/stage1-real-hw-20260908-231300/tools/open_sx_flashtool.sh
   ```

4. 2ND BOOTLOADER: `/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool/firmware/nrc7292_boot.bin`
5. XIP BOOTLOADER: `/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool/firmware/nrc7292_boot_xip.bin`
6. XIP FIRMWARE: `/home/hotaru/silex/nrc7292_xip_fw_sx_uart.bin`
7. XIP BOOTLOADERとXIP FIRMWAREのUploadをON。MAC ADDRESSはMAC0/MAC1ともWRITEをOFF。工場MAC/BDFを変更しない。
8. Refresh後、起動scriptが表示したSXの実デバイス名に対応するポートを選択。GUIでは`ttyUSB0`等が表示されるが、番号を固定で選ばずscriptのby-id照合結果と一致させる。
9. STARTを押し、進捗100%の完了を確認してFlashToolを閉じる。
10. CN2 USBを抜いて電源OFF。SW1 SA: `L L L H L H`。CN2 USBを再接続して電源ON。
11. `standalone ready` と入力。

**SW1は上記Silex指定を使用する。FlashTool内のDIPイラストはpin1が逆とSilexガイドp7に注意書きがある。**

Standalone firmware: 855036 bytes

SHA256: `7e93f02fb67a0d6504998a7372dc01cdcaf47c800c70f5b8224844dc85170ccf`

2ND bootloader SHA256: `33db5706c1be366c07908eb0093ad5a6c1254778149415bf16971bc12c9c4057`

XIP bootloader SHA256: `51da4440d881fd77fdb91c11dcc8d19d81ffbfb76f68c2eaff07fc6826886be4`

Bootloader指定はSilex提供Standalone Start-Up Guideの指定に照合済み。
CN8 console jumpers 5–6、7–8も同ガイド記載。USB-UARTブリッジの認識だけではUART配線の疎通は証明されない。

無線のscan/association/IP試験はCodexが行い、JP Duty/LBTは維持する。
