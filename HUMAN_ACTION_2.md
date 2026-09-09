# HUMAN ACTION REQUIRED #2

想定拘束時間: 5～10分。2026-09-09 18:51 JST、StandaloneによるRF独立確認が完了。custom FWも準備済み。

状態: **書込みへ進める。** Standalone boot、APへのOpen認証association、SX→PCとPC→SXのping各5/5がPASS。Standalone書込みログでもMAC/RF移行skipを確認済み。MAC0/MAC1 WRITE OFFを維持する。

対象（XIP FIRMWARE）:

`/home/hotaru/silex/_codex_work/prepared_sx_20260908_guarded/out/nrc7292/standalone_xip/L2BRIDGE_HSPI/nrc7292_standalone_xip_L2BRIDGE_HSPI_2M.bin`

サイズ: 832972 bytes

実配置は `0x10000..0xdb5cc`（末尾を含まない）、4KiB丸めでも `0xdc000` まで。1MiB内に収まり、通常起動の工場参照先はSYS `0xfe000` / RF `0xff000`。ファイル名の `_2M` だけを理由に再buildする必要はないことをELFとsourceで照合済み。

SHA256: `c05ec7b2bdf3dc94ccba87ac8d543bdd2db1cc4e5ee7ae770df2f57d89fe6a01`

1. Standaloneで `sf size`、`sf memmap`、`sf show_macaddr` の読み取り結果をCodexが保存。
2. SXのCN2 USBを抜いて電源OFF。CN1の追加給電は不要。
3. SW1 Download: `L H H L L H`。
4. CN2 USBを再接続して電源ON。デスクトップ端末で次を実行（Java GUI、sudo/Wine不要）。custom binを引数で渡す必要がある。SX by-idと3種類のbin、MAC WRITE OFFは自動設定される。

   ```sh
   /home/hotaru/silex/_codex_work/worktrees/stage1-real-hw-20260908-231300/tools/open_sx_flashtool.sh /home/hotaru/silex/_codex_work/prepared_sx_20260908_guarded/out/nrc7292/standalone_xip/L2BRIDGE_HSPI/nrc7292_standalone_xip_L2BRIDGE_HSPI_2M.bin
   ```
5. 2ND BOOTLOADER: `/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool/firmware/nrc7292_boot.bin`
6. XIP BOOTLOADER: `/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool/firmware/nrc7292_boot_xip.bin`
7. XIP FIRMWAREが上記guarded custom binであることを確認。GUI内でpathを編集せず、scriptの引数で指定する。
8. MAC ADDRESSはMAC0/MAC1ともWRITE OFF。Upload XIP BOOTLOADER/FIRMWAREをON。工場MAC/BDFは書き換えない。
9. Refreshやmoduleタブ変更をせずSTART。進捗100%を確認してFlashToolを閉じる。完了後SX電源OFF。
10. SW1 SA: `L L L H L H`。SX電源ON。
11. `l2bridge ready` と入力。

このbinはNewracom SDKの純正 `hif.c` / `hif_fifo.c` / `hif_hspi.c` を使用するL2BRIDGE_HSPI。
JP Duty/LBTは有効。Silex BDFは提供archiveから無変更で使用。
SDKの自動Flashマップ移行はguardで抑止し、実際のARM呼び出し先まで検査済み。
実機boot・HSPI・無線は、この書込み後の測定で判定する。

旧試作bin SHA256 `118577528bc061aa69e4ddf2b90a8126d2a2f479df8b4875a66496565fe5d5ba` は自動移行guardがないため使用しない。

30分枠に収まらなければ人間を待機させず、都合のよい時点でこの1回を行う。
