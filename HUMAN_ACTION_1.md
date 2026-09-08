# HUMAN ACTION REQUIRED #1

想定拘束時間: 5～10分。現在の人間作業はSilex Standalone FWの書込み。

状態: **書込み手順へ進める。** ROM/RAM起動、12KiBの工場領域二重読取り、工場レコードCRCを確認済み。実際の旧MAC参照先にも移行markerがなく、RF移行条件も不成立。version更新は既存レコードとbyte一致する。この個体と指定の配布物について、MAC0/MAC1 WRITE OFFで工場MAC・JP校正値を保持する条件を確認した。

## A. 確認済みの構成

- SX/APアンテナ接続、ESP/SX GND共通は人間確認済み。再確認不要。
- ESPの3.3V電源線はSXに接続不要。
- CN8の5–6/7–8は装着済み。JP2はロゴ正立時の右側（2–3、3.3V設定位置）。電圧実測ではないが、追加写真やテスターは要求しない。
- SXはCN2のUSBから給電。CN1のACアダプタ接続は不要。提供Standaloneガイドp13にUSBのみの給電例、JP Startupガイドp8にもUSB給電可と明記。
- SW1は現在Download `L H H L L H`。RAM診断終了後なので、GUIの前にCN2抜き差しでROMから起動し直す。
- APは192.168.10.10、有線疎通済み、SSIDはhalow_demo_video。securityは未取得。
- SX書込みのGUIにsudoは不要。以前のネットワーク試験用許可は03:40に期限切れなので、無線試験用に下記scriptを一度実行する。

ネットワーク試験の準備（通常ユーザーのデスクトップ端末で実行。このscript内でsudoを要求する）:

```sh
/home/hotaru/silex/_codex_work/worktrees/stage1-real-hw-20260908-231300/tools/prepare_privileged.sh
```

AP接続EthernetだけにUDP50000/ICMPの一時受信許可を追加し、4時間後に自動解除する。恒久NixOS設定は変更しない。

SAでは既存FWの応答がなかったが、Download ROMと対応RAM bootloaderは実機で応答済み。CN2単独給電で進める。

## B. Silex Standaloneを書込み、起動を確認

SXへの書込み操作は人間が行う。ESPは診断FWでSPI未開始。CodexのSXモニター/読取りツールは終了済み。

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
工場バックアップ: `/home/hotaru/silex/_codex_work/private_sx_backups/20260909-081543-554105/`。取得内容と書込み範囲の判断は `docs/SX_FLASHTOOL_FACTORY_WRITE_AUDIT_20260909.md` に記録。

無線のscan/association/IP試験はCodexが行い、JP Duty/LBTは維持する。
