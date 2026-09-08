# HUMAN ACTION REQUIRED #2

想定拘束時間: 5～10分。StandaloneによるRF独立確認後、人間がまだ作業可能な場合に実施。
23:27 JSTにguarded custom FW完成。Standalone起動・RF試験は未完了なので、現在は手順の準備のみ。

状態: 書込みは保留。PC側FlashToolにもMAC/RF calの自動migration処理が見つかり、影響を確認中。custom firmware内のmigration guardとは別の処理のため、確認が済むまでSTARTを押さない。

対象（XIP FIRMWARE）:

`/home/hotaru/silex/_codex_work/prepared_sx_20260908_guarded/out/nrc7292/standalone_xip/L2BRIDGE_HSPI/nrc7292_standalone_xip_L2BRIDGE_HSPI_2M.bin`

サイズ: 832972 bytes

SHA256: `c05ec7b2bdf3dc94ccba87ac8d543bdd2db1cc4e5ee7ae770df2f57d89fe6a01`

1. Standaloneで `sf size`、`sf memmap`、`sf show_macaddr` の読み取り結果をCodexが保存。
2. SXのCN2 USBを抜いて電源OFF。CN1の追加給電は不要。
3. SW1 Download: `L H H L L H`。
4. CN2 USBを再接続して電源ON。デスクトップ端末でworktreeの `tools/open_sx_flashtool.sh` を実行（Java GUI、sudo/Wine不要）。
5. 2ND BOOTLOADER: `/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool/firmware/nrc7292_boot.bin`
6. XIP BOOTLOADER: `/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool/firmware/nrc7292_boot_xip.bin`
7. XIP FIRMWAREに上記guarded custom binを指定。
8. MAC ADDRESSはMAC0/MAC1ともWRITE OFF。Upload XIP BOOTLOADER/FIRMWAREをON。工場MAC/BDFは書き換えない。
9. Refresh後、scriptが照合したSXのポートを選んでSTART。進捗100%を確認してFlashToolを閉じる。完了後SX電源OFF。
10. SW1 SA: `L L L H L H`。SX電源ON。
11. `l2bridge ready` と入力。

このbinはNewracom SDKの純正 `hif.c` / `hif_fifo.c` / `hif_hspi.c` を使用するL2BRIDGE_HSPI。
JP Duty/LBTは有効。Silex BDFは提供archiveから無変更で使用。
SDKの自動Flashマップ移行はguardで抑止し、実際のARM呼び出し先まで検査済み。
実機boot・HSPI・無線は、この書込み後の測定で判定する。

旧試作bin SHA256 `118577528bc061aa69e4ddf2b90a8126d2a2f479df8b4875a66496565fe5d5ba` は自動移行guardがないため使用しない。

30分枠に収まらなければ人間を待機させず、都合のよい時点でこの1回を行う。
