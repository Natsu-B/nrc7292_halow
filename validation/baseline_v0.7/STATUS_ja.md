# 検証状態 0.7.0

実行日:2026-09-05。対象はこのZIPのソースおよび付属クリップです。

## 実行・確認したもの

| 試験 | 結果 | 範囲 |
|---|---|---|
| Native C 単体試験 | 7/7 PASS | NRCV/H264/L2/HSPI mock/HVC1/MAC文字列 |
| ASan + UBSan | 7/7 PASS | 上記C単体試験 |
| Node 単体・UDP試験 | 12/12 PASS | 実H264クリップのlocalhost UDP伝送、再構成、欠落、順序、制限 |
| C packetizer → Node receiver | PASS | 360p:277packet /120frame、1080p:344packet /60frame、全ペイロード一致 |
| HVC1 CRC・サイズ検査 | PASS | H264/MJPEG両クリップ |
| H264 elementary stream復号 | PASS | ローカルFFmpegで復号。PCアプリとは別の開発検査 |
| UI/MJPEG表示 | 12/12 PASS | Chromium144、同じrenderer、模擬IPC |
| JS構文、ESP/NRC API stub構文 | PASS | 構文だけ。実ヘッダーABI/リンカ/ハードウェアの証明ではない |
| SDK準備パッチ | PASS | 提供SDK1.7コピー、wlif.c変更、standalone.c未変更、共有protocol一致 |

ブラウザーのURL移動が実行環境ポリシーで禁止されているため、UI試験は独自に生成したHTML/CSS/JSをメモリ上へ注入し、MJPEGを表示しました。ポリシーを変更していません。about:blankはsecure contextではないため、WebCodecs H264経路はこのUI試験では実行していません。スクリーンショットはこのローカル模擬表示であり、実無線映像ではありません。

実 SDK ヘッダーをそのまま native GCC に渡す構文検査も試みましたが、NRC の32bit/newlib型定義とホスト64bit/glibcの int32_t/int64_t 定義が衝突して完了しませんでした。`sdk-header-syntax.log`に記録しています。これをターゲットビルド成功とは数えません。

## 未実施

- ESP-IDF v5.5.2、xtensa-esp32s3-elf による実コンパイル・リンク・flash
- NRC7292 Arm toolchainによる実コンパイル・リンク・flash
- Electron44.2.0本体の起動、Windows portable exeの生成・実行
- Electron/WebCodecsのH264復号表示
- SX-NEWAH-EVK実配線、SPI波形、割込み・復旧、実AP/WPA3接続
- Silex BDF と SDK1.7のRF互換性・独自FWの認証条件確認
- Duty/LBTの実送信時間、AP自身の制約、長距離、高速移動
- 長時間録画、OS電源断、ディスク障害、GPU差異の検証

利用可能なNode/GCC/FFmpeg/Chromiumで行った検査です。ターゲットtoolchainとElectron本体はなく、取得も成功していません。したがって**MCU実行bin、bootloader、PC exeを捏造・同梱していません**。CI定義は実行例であり、CI合格済みという意味ではありません。

## ソース上の主な修正

1. NRCのMACアドレス取得をchar[32]へ変更し、`xx:xx:xx:xx:xx:xx`を6byteへ変換。誤って6byteバッファへ文字列を受けない。
2. Duty marginを0µsへ。CS128µs、pause2000µsを別APIへ設定。
3. SDK互換用のlwIP初期化を維持し、利用者IP処理の入口/出口を切り離す。閉じたmodem ABIの依存を無視しない。
4. HSPI部分転送タイムアウトでホスト送信を停止。古い送信キューは500msで破棄。
5. Ethernet受信バッファをIP入力へ渡した後の二重解放を避ける。
6. PCはフレーム数・サイズ・IPC・録画キューを制限し、H264参照欠落後はIDRを待つ。

この成果物はベンチ試験開始のための実装候補です。実動作や飛翔時の保証はありません。
