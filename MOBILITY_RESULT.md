# 高速なリンク変化への追従：実装・検証結果

2026-09-09。100m/sでAPへ接近・離反する用途を想定し、ESPとPC受信器の応答を短縮した。
実機の配置は約50cm（ユーザー申告）。**100m/s飛行・3km通信は NOT_TESTED**。
実装は通信状態の改善・悪化へ応答するもので、速度や接近・離反方向を推定する機能ではない。
従来の適応試験は `RANGE_RESULT.md`、Stage-1経路の実証は `STAGE1_RESULT.md` を参照する。

映像入力は同じ12秒の録画素材である。「最新」は単一の素材時計に対応する現在位置を意味する。
画質切替や再接続で古い映像へ戻さず、現在位置から送信を再開する。実カメラ入力は未実装・未試験。

| 制御 | 今回の設定・動作 |
| --- | --- |
| feedback処理 | 50msを目安に処理。画像の待機中・fragmentの送信間隔待ちにも実行 |
| 適応判定 | 100ms周期。既存SX統計は100ms通知、500ms以内の受信をfreshとする |
| 往復確認 | 送信条件を満たす間、200ms周期でNRCS v4を送り、PCがNRPAを返信 |
| 実受信feedback | PCは200ms周期でNRFB v3を送信。受信packet・損失・完成frame数を含む |
| probeの採用 | 送信済みtoken・stream・CRC・送信元を確認。RTT 750ms以下を採用、300ms以下を良好とする |
| probeの悪化 | 採用したRTTが500ms以上なら悪化扱い。最後の有効ACKまたは初回probeから750ms超の無応答でも降格 |
| 最初の降格 | 損失・queue圧力・timeoutなどでは、360pの100/500/1000msから先に360p・2秒へ移す。続く悪化では解像度を下げる |
| 悪化直後の回復抑止 | 最後の悪化から500msは昇格を保留。同一悪化の降格連打は250ms未満を抑止 |
| 長い間隔からの回復試行 | 良好probe ACKを3回得ると、120p・4/6/10秒から120p・2秒、最大100kbpsで新しい画像を試す |
| 画質の回復 | 新しい実完成frameのfeedbackと良好probe ACKを2回得て1段階上げる。小さなprobeの成功だけでは解像度を上げない |
| 回復試行の再試行 | 失敗後500ms → 1000ms → 最大2000msの待ち時間を設ける |
| 映像の無応答 | 全fragmentのホスト送信受付後、完成frameの報告が1000ms超ない場合に降格。完成frame数の増加で解除し、probe ACKでは解除しない |
| 再接続 | 対象側の接続試行上限2秒、ホスト待ち2.25秒。失敗後250/500/1000/2000ms、再接続taskは100ms周期で確認 |

これらはソフトウェア内の周期・閾値であり、期限内の無線配送を保証する値ではない。
NRCS/NRPAも実際の無線通信であり、Duty・接続状態・queueの制約を受ける。
手動pauseの無通信は悪化として数えず、手動disconnectは自動再接続要求を解除する。

画質の段階は維持した。

| 段階 | 解像度・更新間隔 |
| --- | --- |
| 0 | 640×360・100ms、既存10fpsのH.264（P-frameあり） |
| 1–3 | 640×360・500ms / 1秒 / 2秒、独立IDR |
| 4 | 426×240・2秒 |
| 5 | 320×180・2秒 |
| 6–9 | 214×120・2秒 / 4秒 / 6秒 / 10秒 |

使用bundleは `.build-range-video/video_profiles.bin`、1,534,484 bytes。
SHA256は `be1c9cad03cea26c1368e8f4a1ea323b3b85b260a9cd2bab6337cd9cf3bb0393`。
SX firmware・native HSPI・JP Duty/LBT・BDF・工場MAC・AP設定は今回変更していない。

| 作業branchの検査 | 状態・証拠 |
| --- | --- |
| Native C | PASS：13 Cテスト |
| Node | PASS：27 Nodeテスト |
| ホスト検査一式 | PASS：`logs/mobility-20260909/host_tests_final.log` |
| ESP build | PASS：`logs/mobility-20260909/esp_build_final.log` |
| ESP flash / boot | PASS：`logs/mobility-20260909/esp_flash.log`、`esp_boot.log` |
| HSPI / EIRQ | PASS：CE0 GPIO6・5MHz、EIRQ GPIO4有効 |
| loopback | PASS：100×64 / 100×512 / 1000×1518。data・sequence・SPI error各0。`logs/mobility-20260909/hspi.log` |
| 急な悪化→回復を2回反復する実機試験 | PASS。`logs/mobility-rapid-20260909/range_result.json`。116秒のESP監視を含むwrapperもexit0 |
| byte照合 | PASS：**250 frame一致、不一致0** |
| 新しい素材時計 | PASS。250 frame・23 stream・単一epoch、相対時計差−159〜+191ms。`source_clock.json` |

対象source commitはESP `965eed1`、受信器 `32a0b9e`、検査ツール `2d0b2a1`。
RANGE_RAPID試験は実際のESP → native HSPI → SX → AP-100AH → Ethernet PC経路を使う。
悪化段階の損失counterだけを合成し、回復feedbackには実受信packet・実完成frame数を使う。
probe ACKも実際のNRCS受信に応じて返す。**合成損失は移動・RF減衰・Duty損失の実測ではない**。
各cycleの降格、120p試行、画質回復の時刻を記録する。最初の作業branch試験では時刻の取得を必須とし、
後続のmain試験では数値的な応答時間上限と実360p frameの復帰も必須とした。

最初の近接実機試験の受信器実行時間は42.26秒。観測された応答は次のとおり。

| 観測項目 | cycle 1 | cycle 2 |
| --- | --- | --- |
| 最初の合成悪化feedback送信から降格観測 | 401ms | 201ms |
| 回復段階開始から120p・2秒の試行観測 | 600ms | 600ms |
| 回復段階開始から180p・2秒への昇格観測 | 1000ms | 1000ms |
| 回復段階開始から360p・100msへの復帰観測 | 3001ms | 3005ms |
| 回復段階開始から実360p frameの受信 | 約2924ms | 約3842ms |

最後の行は `frame_match` のbyte照合とphase時刻から算出した実画像の到着時間で、他の行はtelemetryの観測時間。
100m/sで同じ時間が経過したと仮定すると、401msで約40m、約3秒で約300m進む。
瞬時応答と表現せず、この試験の実測値として扱う。
mainの判定上限は降格600ms、120p試行2秒、最初の画質回復3秒、段階0復帰6秒、
実完成360p画像7秒。近接配置の回帰試験であり、上限内の飛行中通信を保証するものではない。

| mainでの再確認 | 状態 |
| --- | --- |
| merge / merge commit | YES：`a76bbb72b8af8fac51e3fb2e39c22c60de942984` |
| mainから再build / 再flash / boot | PASS。実bootのapp version `a76bbb7` |
| main HSPI / EIRQ / loopback | PASS。CE0・5MHz、GPIO4 EIRQ、100×64 / 100×512 / 1000×1518すべてerror0 |
| mainホスト検査 | PASS。13 C・27 Nodeを含む一式 |
| main RANGE_RAPID・byte照合 | PASS。2周期とも全時間上限を満たす。259 frame一致、不一致0 |
| main監視完了・素材時計 | PASS。116秒監視wrapper exit0。259 frame・23 stream・単一epoch、相対時計差−158〜+186ms |
| 映像中の制御された再接続 | PASS。attempt1 error0、新streamへ移行してもcapture epoch 58989msを維持。`reconnect_during_video.log` |
| GUIの実描画 | PASS。640×360、WebCodecs、4秒間で表示695→735 frame。`gui_final.json` / `gui_final.png` |
| 双方向ping | PASS。PC→ESP 5/5、ESP→PCはpause後5/5。配信中のESP pingは既存の排他条件によりerror259となるため、pauseして再試験した |

mainでの測定値（回復の基準時刻は悪化feedback終了）：

| 項目 | cycle 1 | cycle 2 |
| --- | --- | --- |
| 悪化feedback送信→降格観測 | 200ms | 400ms |
| 120p・2秒での試送信状態へ復帰 | 600ms | 600ms |
| 実受信成功後、180pへ画質回復 | 1000ms | 1000ms |
| 360p・100msの送信設定へ復帰 | 3000ms | 3000ms |
| 実際の360p frameを受信 | 2929ms | 2931ms |

実360p frameは参照profile 0だけに一致するデータとして確認した。設定通知は200ms周期のため、
実画像の到着が設定通知の観測より先になることがある。
証拠は `logs/mobility-main-rapid-20260909/range_result.json`、`range_timeline.jsonl` と
`logs/mobility-main-20260909/`。厳密な時刻上限の検査ツールは `db39777`。

mainからbuild/flashしたappは373,248 bytes、SHA256:
`d3af2ece9a10d67c1d8833fc72bef2a84755c2bd3640fc5a10d9c5b0d4b335a4`。
source merge以降の追加変更は検査ツールと報告・証拠のみで、ESP・受信アプリの実装は同一。

mainの検証は作業branchのPASSで代用しない。今回の改修・試験に追加の人間操作は要求していない。
進行中の実機試験は既存の一時試験許可内で行う。
23:03のsetupに対応するtimerを確認したが、それ以前のcleanup予約も残っているため、現在のPC受信許可は
**2026-09-10 01:45:38 JST**に失効する予定。恒久NixOS設定の変更は行っていない。
受信GUIはmainの更新版で再起動済み、通常feedbackを有効にして360pへ復帰した。
本FWは引き続き検証用で、起動時にはSPI/RFを自動開始しない。確認済みflagsを指定した試験script、
またはconsoleの `wiring-confirm` → `eirq` → `rf-confirm` → `start` で開始する。

再現は本変更のmain反映後、既存GUI受信と動画送信を停止してUDP 50000を解放してから行う。
PCのEthernet設定は `192.168.10.250/32`、AP `192.168.10.10/32`・ESP `192.168.10.20/32`
へのhost route。APのSSIDは `halow_demo_video` / Open、ESPは `192.168.10.20/24`。

```sh
cd /home/hotaru/silex/halow_stage1_main
./tools/idf_env.sh bash tools/run_tests.sh .native-build-mobility
./tools/build_esp.sh

STAGE1_WIRING_CONFIRMED=1 STAGE1_RF_CONFIRMED=1 \
ESP_IP=192.168.10.20 PC_IP=192.168.10.250 UDP_PORT=50000 \
FLASH_VIDEO=1 VIDEO_CLIP="$PWD/.build-range-video/video_profiles.bin" \
VIDEO_PROFILES="$PWD/.build-range-video/video_profiles.bin" \
TEST_RANGE=1 RANGE_RAPID=1 LOG_DIR="$PWD/logs/mobility-main-retest" \
./tools/test_stage1.sh --eirq
```

bundleがない場合は `nix-shell -p ffmpeg --run 'python3 tools/prepare_video_profiles.py'` で生成し、
上記SHA256を照合する。同じapp・bundleのflashを省いて再試験する場合は `SKIP_FLASH=1` を使用する。
検査scriptが監視時間を計算するため、短いmonitor時間は指定しない。

100m/sでは100msに10m、200msに20m、1秒に100m、10秒に1km移動する。
925.5MHzで正面接近・離反する片道Dopplerの自計算は `f × v / c` により約±308.7Hz。
この値だけでは復調器の追従性能や100m/sでの接続維持を判断できない。
確認した[NRC7292公式仕様](https://newracom.com/products/nrc7292)には最大移動速度・Doppler耐性の保証値は記載されていない。
[SilexのSX-NEWAH＋AP-100AHロケット事例](https://www.silex.jp/library/case/createrocket)は開発・地上伝送試験の紹介であり、100m/s飛行の測定結果ではない。

[対象版Newracom SDK](https://raw.githubusercontent.com/newracom/nrc7292_sdk/f10910b541f070e8a636a891efc0895fe39cfb1f/package/standalone/sdk/include/api_wifi.h)では、
RSSIは直近受信packet、平均RSSIは直近4 packet、MCSは直近の送受信data frameで更新される。
新しいHSPI統計の受信は、新しいRF測定の発生を意味しない。このため回復判断には実際の新しい通信成功を要求する。
撮影時刻と受信時刻の同期測定はなく、zero-delay・100ms以内の画像到着・Duty停止中の10秒以内配送は保証しない。
素材時計の検査も相対的な進行確認であり、一定のend-to-end遅延そのものを測るものではない。
