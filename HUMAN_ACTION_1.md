# HUMAN ACTION REQUIRED #1

想定拘束時間: 5～10分。現在の人間作業はSilex Standalone FWのGUI書込み再試行。

状態: **書込み手順へ進める。** ROM/RAM起動、12KiBの工場領域二重読取り、工場レコードCRCを確認済み。実際の旧MAC参照先にも移行markerがなく、RF移行条件も不成立。version更新は既存レコードとbyte一致する。この個体と指定の配布物について、MAC0/MAC1 WRITE OFFで工場MAC・JP校正値を保持する条件を確認した。

## A. 確認済みの構成

- SX/APアンテナ接続、ESP/SX GND共通は人間確認済み。再確認不要。
- ESPの3.3V電源線はSXに接続不要。
- CN8の5–6/7–8は装着済み。JP2はロゴ正立時の右側（2–3、3.3V設定位置）。電圧実測ではないが、追加写真やテスターは要求しない。
- SXはCN2のUSBから給電。CN1のACアダプタ接続は不要。提供Standaloneガイドp13にUSBのみの給電例、JP Startupガイドp8にもUSB給電可と明記。
- SW1は現在Download `L H H L L H`。GUI失敗後のROM PINGへ応答しているので、今回の再試行前の電源再投入は不要。
- APは192.168.10.10、有線疎通済み、SSIDはhalow_demo_video。securityは未取得。
- SX書込みのGUIにsudoは不要。ネットワーク試験用許可は08:39:30に再設定済み、12:39:30に自動解除予定。今は追加のsudo操作不要。

ネットワーク試験の準備に人間が実行済みのscript（今の再実行は不要）:

```sh
/home/hotaru/silex/_codex_work/worktrees/stage1-real-hw-20260908-231300/tools/prepare_privileged.sh
```

AP接続EthernetだけにUDP50000/ICMPの一時受信許可を追加し、4時間後に自動解除する。恒久NixOS設定は変更しない。

SAでは既存FWの応答がなかったが、Download ROMと対応RAM bootloaderは実機で応答済み。CN2単独給電で進める。

## B. Silex Standaloneを書込み、起動を確認

SXへの書込み操作は人間が行う。ESPは診断FWでSPI未開始。CodexのSXモニター/読取りツールは終了済み。

1. エラーで停止した古いGUIを閉じる。
2. 現在のSW1 Download `L H H L L H`、CN2 USB接続を維持。
3. デスクトップ端末で以下を実行（Java GUI、sudo/Wine不要）。SXの固有by-idと下記3ファイルを自動選択する。STARTは自動実行しない。

   ```sh
   /home/hotaru/silex/_codex_work/worktrees/stage1-real-hw-20260908-231300/tools/open_sx_flashtool.sh
   ```

4. 2ND BOOTLOADER: `/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool/firmware/nrc7292_boot.bin`
5. XIP BOOTLOADER: `/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool/firmware/nrc7292_boot_xip.bin`
6. XIP FIRMWARE: `/home/hotaru/silex/nrc7292_xip_fw_sx_uart.bin`
7. XIP BOOTLOADERとXIP FIRMWAREのUploadをON。MAC ADDRESSはMAC0/MAC1ともWRITEをOFF。工場MAC/BDFを変更しない。
8. **Refreshやmoduleタブ変更は不要。** 自動選択された長いSXのby-idを維持する。元GUIのRefreshは最後のポートへ選び直すため、このPCでは既定がESP側の`ttyACM0`になることを再現確認した。設定を変更してSTARTがブロックされた場合は、GUIを閉じてscriptから開き直す。
9. STARTを押し、進捗100%の完了を確認してFlashToolを閉じる。
10. CN2 USBを抜いて電源OFF。SW1 SA: `L L L H L H`。CN2 USBを再接続して電源ON。
11. `standalone ready` と入力。

今回の「Pinging 2nd bootloader failed」はflash erase/program開始前の停止。配布GUIと同じJava PINGをSXへ直接実行すると6msで成功した。前回ユーザーが選択したポートは未取得なので、原因を誤選択と断定はしない。

起動と人間がSTARTを押した時点の設定、通信経過は `logs/sx_flashtool_日時-PID.log` に自動保存する。再失敗時はSTARTを繰り返さずGUIを閉じれば、Codexがこのログを調べられる。

**SW1は上記Silex指定を使用する。FlashTool内のDIPイラストはpin1が逆とSilexガイドp7に注意書きがある。**

Standalone firmware: 855036 bytes

SHA256: `7e93f02fb67a0d6504998a7372dc01cdcaf47c800c70f5b8224844dc85170ccf`

2ND bootloader SHA256: `33db5706c1be366c07908eb0093ad5a6c1254778149415bf16971bc12c9c4057`

XIP bootloader SHA256: `51da4440d881fd77fdb91c11dcc8d19d81ffbfb76f68c2eaff07fc6826886be4`

Bootloader指定はSilex提供Standalone Start-Up Guideの指定に照合済み。
工場バックアップ: `/home/hotaru/silex/_codex_work/private_sx_backups/20260909-081543-554105/`。取得内容と書込み範囲の判断は `docs/SX_FLASHTOOL_FACTORY_WRITE_AUDIT_20260909.md` に記録。

無線のscan/association/IP試験はCodexが行い、JP Duty/LBTは維持する。
