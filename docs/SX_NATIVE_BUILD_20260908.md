# SX native HSPI build evidence — 2026-09-08

`custom SX build = PASS`。NRC7292実ARMリンクと生成binを確認した。SXへの書込み・起動・無線試験はこの記録の対象外で、未実施。

## 書込み候補

```text
/home/hotaru/silex/_codex_work/prepared_sx_20260908_guarded/out/nrc7292/standalone_xip/L2BRIDGE_HSPI/nrc7292_standalone_xip_L2BRIDGE_HSPI_2M.bin
bytes: 832972
SHA256: c05ec7b2bdf3dc94ccba87ac8d543bdd2db1cc4e5ee7ae770df2f57d89fe6a01
```

旧 `prepared_sx_20260908_native` 内のbin（832964 bytes、SHA256 `118577528bc061aa69e4ddf2b90a8126d2a2f479df8b4875a66496565fe5d5ba`）は **DO_NOT_FLASH / 廃止**。下記のSDK自動移行処理を含むため、ビルド成功後に書込み候補から除外した。

## 実際に解消した問題

- Gitから取得したSDKは一部テスト用symlinkが切れており、従来の`copytree`が失敗した。準備時にsymlinkをそのまま保存するよう修正した。
- SDK 1.7のビルド既定値は`profile1`。既存flash mapの移行を要求するため、`build_sx.sh`で`CONFIG_FLASH_PROFILE=factory`を明示した。これはリンク時の容量制限であり、実機のmapを書き換える操作ではない。
- さらに実ARM逆アセンブルで、`nrc_sf_init_memory_map()`が旧形式の2 MiB mapに対し自動移行する経路を発見した。`nrc_sf_migration_1m_to_2m()`は0x1f7000以降を消去し、旧SYS_CONFIG/RF_CALを0x1fc000/0x1fd000へ複写する。その後mapを0xf000へ保存する。リンク設定をfactoryにするだけではこの動作は止まらない。
- コピーしたSDKの`libModem.a`だけで当該symbolをweakにし、`sx_flash_guard.c`のstrong定義でSDK既定の`SF_MIGRATION_RES_ERROR`（4）を返す。元のSDK・元のライブラリ・Silex BDF・純正HSPIソースは変更しない。
- 最終ARM命令は`movs r0,#4; bx lr`の2命令。initializerからの呼出しはveneer経由でこの定義へ到達する。initializerの条件は`result <= 1`または`result == 5`の場合だけmap保存へ進むため、4では書込みブロックを通らない。

既存mapは引き続きSDKが読み込む。mapが存在しない1/2 MiB flashではRAM上のfactory fallback（SYS_CONFIG 0xfe000、RF_CAL 0xff000）を使う。4 MiBはSDKの4 MiB mapを使う。破損した既存mapの修復をこの変更では行わない。

`user_init`、`_hif_open`、`nrc_wifi_init`、`nrc_sf_sysconfig_load`の直接呼出しグラフに追加のSF write/erase経路は見つからなかった。間接呼出しや実機動作を含む全面的な保証ではなく、今回確認した自動移行の防止を実ARMリンクで検証した結果である。

## 入力と検証

- Newracom SDK 1.7.0、Git revision `f10910b541f070e8a636a891efc0895fe39cfb1f`。現行masterへ差し替えていない。
- GCC `arm-none-eabi-gcc 7.3.1 20180622` / GNU Tools for Arm Embedded Processors 7-2018-q2-update。
- GCC archive SHA256 `bb17109f0ee697254a5d4ae6e5e01440e3ea8f0277f2e8169bf95d07c7d5fe69`。
- Silex BDFは提供済みdriver archiveの`nrc7292_driver/nrc_pkg/boarddata/nrc7292_bd.dat`。644 bytes、SHA256 `c4550702d2409dbdc1b2ce6e0f81a77840d7de7887def0359a8cdb4e8d9eac41`。最終bin内のoffset 578342に同一644 bytesが1回存在する。
- `hif.c`、`hif.h`、`hif_fifo.c`、`hif_hspi.c`、`hif_dma.h`は記録済みSDK純正ソースとbyte一致。UART standalone FWを改造したものではない。
- アプリはJP country、TX time/CCA、Duty有効化を維持し、guard設定失敗時のassociationを拒否する。工場MAC設定APIは追加していない。
- factory linker: FLASH origin 0x10000 / length 0xe5000（916 KiB）。生成binは範囲内。
- 2m/4m aliasの差を確認するため旧候補を両方ビルドしたところbinは完全一致した。SDKのサイズ指定はoutput名とリンク上限に使われ、実行時はflash情報を読む。**実機のflash容量・mapは未取得**であり、2 MiB実機と認識できたという意味ではない。
- `tests/check_sx_flash_guard.py`は保護前ELFを拒否し、保護後ELFでPASS。guardのARM命令と実際の呼出し先を検査する。

## 再ビルド

NixOSの恒久設定変更、sudo、システムtoolchain置換は不要だった。

```bash
cd /home/hotaru/silex/_codex_work/worktrees/stage1-real-hw-20260908-231300
bash tools/build_sx.sh \
  /home/hotaru/silex/_codex_work/prepared_sx_20260908_guarded \
  /home/hotaru/silex/_codex_work/gcc-arm-none-eabi-7-2018-q2-update/bin \
  2m
```

新しいsourceを取り込む場合は既存の準備先を上書きせず、`tools/prepare_sx_sdk.py`へ新しい`--output`を指定する。元SDKは`/home/hotaru/silex/_codex_work/nrc7292_sdk/package/standalone`、元BDFは`/home/hotaru/silex/_codex_work/nrc7292_driver/nrc_pkg/boarddata/nrc7292_bd.dat`。

## 人間がFlashToolへ指定するファイル

提供済みSilex Standalone Start-Up Guide（WA107660XA、8ページ）が次のbootloader組合せを指定している。

```text
Tool:
/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool/FirmwareFlashTool.exe
2ND BOOTLOADER:
/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool/firmware/nrc7292_boot.bin
XIP BOOTLOADER:
/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool/firmware/nrc7292_boot_xip.bin
XIP FIRMWARE:
上記のguarded bin
MAC ADDRESS WRITE: OFF
```

2ND SHA256: `33db5706c1be366c07908eb0093ad5a6c1254778149415bf16971bc12c9c4057`。
XIP bootloader SHA256: `51da4440d881fd77fdb91c11dcc8d19d81ffbfb76f68c2eaff07fc6826886be4`。

Standaloneで無線を独立確認した後、可能ならCLIの読取り専用`sf size`、`sf memmap`、`sf show_macaddr`を保存し、この候補へ進む。Download時のSW1は`L H H L L H`、電源OFF後SAは`L L L H L H`。実機互換性の最終確認は書込み後のboot、工場MACの読取り、HSPI probeで行う。CodexはSXへ書き込んでいない。
