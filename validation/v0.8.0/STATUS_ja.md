# v0.8.0 検証結果

## 配布状態

ソース・ホスト試験済み。ESP32-S3 / NRC7292の実行bin・ELFは未生成。Electron完成exeも未生成です。今回の試験は接続先のNixOSではなく、作業用Linuxコンテナで実施しました。

| 項目 | 結果 | 範囲 |
|---|---|---|
| C単体・模擬統合試験 | 11/11 PASS | gccによるホスト実行 |
| native HSPI可変レコード | PASS | 1518 byte×1000回。レジスタ/slot mock、実nativeホストコード |
| 実ESPドライバの模擬統合 | PASS | pthread/FreeRTOS shim、native HAL mock、128可変長echo、EIRQ、poll、timeout、RX所有権、fault/stop/restart |
| 上記ESP模擬統合の反復 | 10回 PASS | 各回独立起動 |
| 実SXアプリのecho処理 | PASS | SDK/RTOS shim、37-byte分割入力、10秒host不在、pool枯渇、I/Oエラー |
| ASan / UBSan | 11/11 PASS | Clang、ホスト実行。ESP/SX実機ではない |
| Node receiver | 12/12 PASS | 実Node実行、localhost UDPを含む |
| C packetizer→Node receiver 360p | PASS | 277 packet / 120 frame、全byte一致 |
| 同1080p | PASS | 344 packet / 60 frame、全byte一致 |
| Flash安全確認ツール | 6/6 PASS | パーティションMD5、重複/異常配置、clip CRC。実書込みなし |
| native原本・共有ABI | PASS | 7原本hash、host/SXのL2 c/h一致 |
| APIスタブ構文確認 | PASS | 本物のSDKビルドではない |
| Clang static analyzer | 6 source、診断なし、exit 0 | SDK部分はstub |
| GCC -fanalyzer | 未完了 | コマンド時間上限で停止。PASS扱いしない |
| SDK 1.7への適用 | PASS | 添付SDKとSilex由来BDFの別コピー上 |
| ESPターゲットビルド | BLOCKED | idf.py / Xtensaコンパイラなし。build script exit 2 |
| NRCターゲットビルド | BLOCKED | arm-none-eabi-gcc/g++なし。build script exit 2 |
| toolchain取得 | BLOCKED | dl.espressif.com名前解決失敗 |
| 実HSPI / EIRQ / GPIO電圧 | NOT TESTED | 実機未接続 |
| AP-100AH接続・RF・Duty | NOT TESTED | 実機未接続 |
| Electron GUI / H.264表示 | NOT TESTED | Electron本体未実行 |
| ユーザーPC flash / main merge | NOT PERFORMED | ユーザーNixOSへの接続なし |

## 今回の入力

- 添付 `halow_stage1_v0.7.1-source.zip` を基準に専用worktreeで変更。
- 添付Newracom SDKのVERSION-SDKは1.7.0。
- SDK ZIPのcommentは`f10910b541f070e8a636a891efc0895fe39cfb1f`。現行masterと同じだとは仮定していない。
- SDK適用試験のBDFは、添付Silex `sx-newah_driver-v134rev30_20240312.tar.gz` 内の `nrc7292_driver/nrc_pkg/boarddata/nrc7292_bd.dat`。
- BDF SHA256は`c4550702d2409dbdc1b2ce6e0f81a77840d7de7887def0359a8cdb4e8d9eac41`。由来を確認できたことと、SDK1.7とのRF互換・認証条件が確認できたことは別。後者は未検証。

## 再現

```bash
bash tools/run_tests.sh /tmp/halow-native
CC=clang cmake -S tests -B /tmp/halow-asan -G Ninja -DSANITIZE=ON
cmake --build /tmp/halow-asan
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir /tmp/halow-asan --output-on-failure
```

v0.7のログは`validation/baseline_v0.7/`へ分離しました。過去版の測定・GUIスクリーンショットをv0.8の実機結果として流用していません。配布zipの展開後再試験のログは別添のdelivery-validation.txtを参照してください。

## 次の実機確認

まず対応SDKとコンパイラで両方の実行binを生成し、SXは人間が書込み、S3は識別済みポートへCodexが書き込む。その後、未接続状態の5 MHz HSPIで`probe`、`loopback 1000 1518`を行う。RF・映像はその成功後です。

本版は地上実証用です。LoRa指令、飛翔判定、airtimeの永続台帳、カメラ/H.264エンコード、完全な動的ビットレート制御は含みません。
