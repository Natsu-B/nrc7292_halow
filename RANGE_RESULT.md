# 距離拡張に向けた映像適応・再接続の実機結果

2026-09-09。現在の約50cm配置（ユーザー申告）で、画質・更新間隔の切替と映像受信を確認した。
目標の **3km通信は NOT_TESTED**。従来の通信経路の実証は `STAGE1_RESULT.md` を基準とし、
本書はその後のESP・受信ソフト変更を記録する。

映像入力は既存の **12秒の録画素材**。実カメラの機種・入力方法は未確定である。
「最新」は素材を繰り返す単一時計の現在位置を意味し、実カメラのライブ撮影を実証したものではない。
画質切替・再接続で素材の先頭へ戻らず、古い映像を蓄積して順送りしない。

| 段階 | 解像度 | 送信更新間隔 |
| --- | --- | --- |
| 0 | 640×360 | 100ms（既存10fps、P-frameあり） |
| 1–3 | 640×360 | 500ms → 1秒 → 2秒 |
| 4 | 426×240 | 2秒 |
| 5 | 320×180 | 2秒 |
| 6–9 | 214×120 | 2秒 → 4秒 → 6秒 → 10秒 |

段階1以降はSPS/PPSを含む独立IDRを使用する。受信feedback、queue、Duty状態などに応じて
段階を下げ、良好なfeedbackが継続した場合に戻す。RSSIの閾値は暫定的な判断材料であり、
距離換算や3km到達の根拠にはしない。手動disconnectでは接続要求を解除し、自動再接続しない。

| 実施項目 | 結果・証拠 |
| --- | --- |
| 実データ経路 | ESP → native HSPI → SX → AP-100AH → Ethernet PCで受信 |
| 近接配置での適応試験 | PASS。`logs/range-nearbench-20260909/range_result.json` |
| 受信frame照合 | **187 frameすべて素材byte一致、不一致0**。照合payload 453,628 bytes |
| 全画質の受信 | 360p・240p・180p・120pを受信。最低段階9を観測し、悪化段階中にも120pを4 frame照合 |
| 受信処理 | invalid / duplicate / out-of-order / expired / reference-dropは各0 |
| 品質回復 | 良好feedbackを10回送った後の段階9→8への回復を観測 |
| 素材時計 | **187 frame、15 streamで単一epoch 58362msを維持**。各PTSと素材frame位置も一致 |
| 受信時刻と素材PTSの相対進行 | PASS。最初の照合frameを基準にした差は **−159〜245ms**、検査許容幅±3000ms以内 |
| 映像中の再接続 | 制御コマンドによる切断後、再接続error0。streamを更新してepoch **367340ms**を維持 |
| ホスト検査・ESP build/flash | 作業branchでPASS。`logs/range-20260909/host_tests_final.log`、`esp_build.log`、`esp_flash_final.log` |

適応試験では実機の映像を受信しながら、テスト受信器が悪化・回復feedbackを合成して送った。
`syntheticFeedback=true`、`productionAutomaticFeedback=false` であり、**実測のRF損失・
遠距離減衰を再現した試験ではない**。受信結果・切替・時計の根拠は同directoryの
`range_timeline.jsonl` と `esp.log`。再接続の根拠は
`logs/range-20260909/gui_start.log` と `reconnect_during_video.log`。
電波が実際に届かなくなった状態からの復帰、屋外3km、実カメラ入力は未試験である。

更新間隔の設定上限は10秒だが、Duty停止・通信断の間にも10秒以内に画像が届くという保証はない。
snapshot送信の2秒期限はESP側で送信処理へ渡す期限であり、受信・表示までの遅延上限ではない。
端末間の撮影時刻同期によるend-to-end遅延測定も行っていない。
相対時計の検査は受信側時刻の経過と素材PTSの進行を比較するため、一定の配送遅延そのものは検出しない。
今回の変更でSX firmware、JP Duty/LBT、BDF、工場MAC、AP設定は変更していない。

媒体は5本のHVC1を格納したHVB1 bundle。元の `assets/video.bin` はそのままprofile 0へ格納した。
追加4素材は各60 frame・5fps・12秒で、全frameのCRCと独立IDR、実デコード時の解像度を検証した。

- 生成先: `.build-range-video/video_profiles.bin`（Git管理外）
- サイズ: **1,534,484 bytes**（video partitionの2MiB以内）
- SHA256: `be1c9cad03cea26c1368e8f4a1ea323b3b85b260a9cd2bab6337cd9cf3bb0393`
- manifest: 作業worktreeの `.build-range-video/video_profiles.json`。mainで生成手順を実行すると同名で生成する。

再現はmain checkoutで行う。PC Ethernetは `192.168.10.250/32` と
`192.168.10.10/32`・`192.168.10.20/32` へのhost route、ESPは `192.168.10.20/24`。
APは `192.168.10.10`、SSIDは `halow_demo_video` / Open。既存の動画送信を停止し、GUIの受信も
StopしてUDP 50000を解放する。下記はESPのappと動画partitionだけを書き、SXは既存FWを使用する。

```sh
cd /home/hotaru/silex/halow_stage1_main
nix-shell -p ffmpeg --run 'python3 tools/prepare_video_profiles.py'
sha256sum .build-range-video/video_profiles.bin
./tools/build_esp.sh
FLASH_VIDEO=1 VIDEO_CLIP="$PWD/.build-range-video/video_profiles.bin" ./tools/flash_esp.sh
STAGE1_WIRING_CONFIRMED=1 ./tools/test_hspi.sh --eirq

STAGE1_WIRING_CONFIRMED=1 STAGE1_RF_CONFIRMED=1 \
ESP_IP=192.168.10.20 PC_IP=192.168.10.250 UDP_PORT=50000 \
VIDEO_PROFILES="$PWD/.build-range-video/video_profiles.bin" \
LOG_DIR="$PWD/logs/range-main-retest" \
./tools/test_range.sh
```

確認flagsは既に済ませた配線・JP RF確認を表す。`flash_esp.sh` はESPをby-idで特定し、動画の
書込みでは実partition表と書込み後byte一致を検証する。`test_range.sh` は上記の合成feedback
試験を実行して `range_result.json`、`range_timeline.jsonl`、`esp.log`、`source_clock.json` を残す。
3kmの実地試験結果として扱わない。

統合スクリプト変更 `52f631f` もmainへ反映済み。flash・boot・HSPI/EIRQ・映像適応をまとめて再実行できる。
上記のbuildとbundle生成後、個別flash・`test_range.sh` の代わりに次を使う。

```sh
STAGE1_WIRING_CONFIRMED=1 STAGE1_RF_CONFIRMED=1 \
ESP_IP=192.168.10.20 PC_IP=192.168.10.250 UDP_PORT=50000 \
FLASH_VIDEO=1 VIDEO_CLIP="$PWD/.build-range-video/video_profiles.bin" \
VIDEO_PROFILES="$PWD/.build-range-video/video_profiles.bin" TEST_RANGE=1 \
LOG_DIR="$PWD/logs/stage1-range-main-retest" ./tools/test_stage1.sh --eirq
```

ホスト検査はmainで `./tools/idf_env.sh bash tools/run_tests.sh .native-build-range` がPASS。
通常shellで不足していたNinja/compilerは、この既存repo-local環境で解消した。

| main確認項目 | 状態 |
| --- | --- |
| source merge | YES：`af98b59a706a40954a1598648147199392e9ed1c` |
| mainからのESP再build | PASS。app 371,808 bytes、下記SHA256 |
| mainからのESP app再flash / boot | PASS |
| mainホスト検査 | PASS。13 C・21 Nodeを含む既存検査と追加検査 |
| main HSPI / EIRQ / loopback | PASS。CE0・5MHz・EIRQ有効、100×64 / 100×512 / 1000×1518すべてerror0 |
| main 360p・60秒受信 | PASS。590 frame byte一致、不一致0、同一streamの連続120 frame cycleも一致 |
| mainでの適応・byte照合 | PASS。178 frameすべてbyte一致、不一致0。120p・10秒到達、良好feedback10回後に120p・6秒へ回復 |
| main素材時計 | PASS。178 frame・13 stream・単一epoch。受信span 70,007ms、相対時計差 −172〜+399ms（許容±3000ms） |
| main双方向ping | PASS。PC→ESP、ESP→PCとも5/5 |
| main GUI表示 | PASS。640×360、WebCodecs、4秒間で表示395→435 frame。通常feedback有効 |

main app SHA256:
`1fb40d897f9b3cf2937978866015ea0a483ffeea3ccb566c1136186e15e57830`。
main 360pの結果は `logs/range-main-video-20260909/video_result.json`。
main適応試験は検証ツールの修正後に実行し、147秒の監視を含むwrapperもexit0。
証拠は `logs/range-main-adapt-20260909/range_result.json`、`source_clock.json` と
`logs/range-main-20260909/`。ESP再起動で合成試験の適応状態を解除し、通常の360p GUI受信へ戻した。
GUI画像は `logs/range-main-20260909/gui_final.png`。
appをbuild/flashしたsource merge以降の変更はテストツール・報告・証拠のみで、ESP/SX/受信アプリのsourceは同一。
今回のready応答以降、追加の人間操作は要求していない。

22:42 JSTに通常GUI受信を確認。一時的なPC firewall許可は既存timerにより **22:47:28 JST** に失効予定。
別のcleanup予約が22:55:09と翌01:45:38にも残っているため、既存 `tools/prepare_privileged.sh setup`
の再実行だけでは古い予約を延長できない。継続運用時にはこのruntime許可の期限管理も必要となる。
恒久NixOS設定は変更していない。起動直後は従来どおりSPI/RFを自動開始せず、再起動後は確認済みflagsを
使った試験script、またはconsoleの `wiring-confirm`、`eirq`、`rf-confirm`、`start` で開始する。
