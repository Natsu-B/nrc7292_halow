# v0.8.0 Native HSPIの実装境界

## 純正のまま使う部分

SX側: SDK 1.7.0 `hif.c`、`hif.h`、`hif_fifo.c`、`hif_hspi.c`、`hif_dma.h`。
SPIコマンド、CRC7、ACK 0x47、送受信queue、4-byte slot header、最大512-byte slot、slot sequence、EIRQ、creditの仕組みはNewracomのものです。汎用SPI slave APIに置き換えたり、裸のEthernetを独自のSPI波形で送ったりしません。

ESP側: 純正ホスト`nrc-hspi.c/h`の移植・修正版です。原本はthird_party内に分離し、差分が追跡できるようにしました。CS境界は参照コードの`spi_transfer`コール単位を保ちます。DMAには内部RAMのバウンスバッファを使います。

## なお必要なレコード

HIFはバイト列です。1回のアプリ書込みが複数slotへ分割され、複数回の読出しにまたがります。slot sequenceはslotの連番であり、制御要求IDやEthernetフレーム長ではありません。従って既存NB/v1の16-byteレコードを維持します。

```text
magic(2) / version(1) / type(1) / flags(2) / seq(2)
length(2) / header CRC16(2) / payload CRC32(4)
```

Ethernetフレームをslotごとの独自fragment番号で再分割する層は追加しません。レコード長が揃うまで待つだけです。CRC32は16-entry nibble table実装で、結果は従来版と一致します。slot trailerのCRCは純正ホスト同様、読出し・破棄するだけです。CRC7やslot sequenceだけでpayload全体を保護できるとは扱いません。

## 追加した診断

`LOOPBACK_REQ=13` / `LOOPBACK_RSP=14`、feature bit9を追加。payload長は1〜1518、応答は要求と同じsequenceです。完全なpayload比較を行います。古いSX FWが機能bitを返さない場合、ホストAPIはNOT_SUPPORTEDを返します。

## 同時実行と異常系

- 初期化後のnative APIは単一I/Oタスクのみが呼ぶ。ESP側のアプリ/lwIPはコピーをキューに入れる。
- 送信中レコードへ別レコードを混在させない。最大2 slot分の送信の間にRXを処理する。
- 制御を優先するのはレコード境界。送信途中にtypeを割り込ませない。
- GPIO ISRはSPIを呼ばない。level割込みを一時maskし、RX/status処理後にrearmする。
- 0 creditは有効な値。100 ms watchdogは割込み喪失対策で、無線のMCS制御周期ではない。
- 単発のHAL転送失敗でも、queue書込みが途中まで成立したか不明なら勝手に再送しない。ホストセッションを停止する。
- I/Oタスクは終了準備完了を通知後にsuspendし、stop側で解放する。DMAやドライバlockを所有中に強制削除しない。
- SX側の単なるcredit待ちは異常ではない。ESPを書き込んでいる間も、現在の最大1616-byteレコードを保持し、他タスクを動かす。キューは固定pool、統計は最新1件だけ。
- NRC側の本当のHIF書込みエラーではセッションを停止する。復旧は実機状態を確認して行い、無線再起動やDuty履歴消去を自動で繰り返さない。

## IPスタック

ユーザーIPv4/ARP/UDPはESP側です。NRC側はWPA/無線MACとEthernetの受け渡しを担当します。ただし閉じたSDKライブラリとのABI互換のため、NRCのidle lwIP/netif初期化は残しています。「lwIPのコード・タスク・RAMをNRCから完全除去した」ものではありません。

## 検証と未検証

ホスト上で実際のESPドライバソースをpthread/FreeRTOS shimとnative register/slot mockへ接続し、HELLO、可変長echo、level IRQ、ポーリング、credit待ちtimeout、受信buffer解放、fault/stop/restartを試験しました。NRCアプリの実parser/echoもSDK shim上で試験しています。

これらはESP-IDF実行、NRC HAL実行、無線相互接続試験ではありません。電気タイミング、ブートローダー/BDF対応、実IDF/Armリンク、長時間Duty挙動、AP-100AH接続、実映像表示は実機で別途確認が必要です。

## 参照元

- 添付 `nrc7292_sdk-master.zip` / Standalone SDK 1.7.0
- https://github.com/newracom/nrc7292_sdk
- https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/peripherals/spi_master.html
- https://github.com/espressif/esp-idf/blob/v5.5.2/components/esp_netif/lwip/netif/ethernetif.c
- 添付 Silex Standalone Start-Up Guide WA107660XA、およびDS202312データシート
