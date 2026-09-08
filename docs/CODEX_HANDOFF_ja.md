# Codexへ引き継ぐ作業

対象のNixOS側repo: `/home/hotaru/silex/halow_stage1_v0.7.1`。
今回の配布物は、そのユーザーrepoを直接変更していない。

## 許可範囲

ESP32-S3は識別済みUSBポートに限り、実ビルド・flash・映像partition書込み・resetを自動実行してよい。NVSを含む全消去、eFuse/セキュアブート設定変更はしない。
SX-NEWAHのflashとSW1操作は人間に依頼する。既存MAC・BDF・Flash profileを推測して変更しない。RF設定の解除は禁止。

## Worktree

1. `git status --short --branch`と`git worktree list`を保存する。
2. mainが存在するか確認し、新しい`codex/native-hspi-v08`等のbranch/worktreeをmainから作る。元repoの未commit変更をstash/破棄しない。
3. 配布したv0.7.1→v0.8.0 patchを`git apply --check`する。元ソースが既に変わっていれば、差分をレビューして適用する。機械的な上書きはしない。
4. 元repoに.gitがない場合は、ユーザーが実際に利用中のrepoを特定する。無関係なmainを作って「mainへ反映済み」と報告しない。

## 最初の到達点

GUIより先に、実MCUのビルドとHSPI診断に集中する。30分は目標であり、未準備toolchainやSX修正がある場合に虚偽の完了を出さない。

```bash
bash tools/preflight.sh
bash tools/run_tests.sh /tmp/native-v08
bash tools/build_esp32.sh --menuconfig
```

実配線に合わせてGPIO、コンソールUSB/UARTを設定する。defaultでは無線・SPIを開始しない。ESPへflash後、シリアルから`pins`を取得する。

SXはSDK 1.7へ適用して実Armリンクし、binのサイズ/SHA/Flash容量/bootloader対応を確認してから、一回にまとめた書込み依頼を人間へ出す。未ビルドのbinパスを提示しない。既存Silex UART firmwareで代用しない。

## HSPI診断

```text
wiring-confirm
probe
diag
loopback 1000 1518
```

5 MHzで要求回数=成功回数、ACK/header/sequence/SPIエラー0を確認する。次にEIRQ配線・level IRQを確認する。10/16 MHzは5 MHzの成功後。

AP接続・映像は別段階:

```text
rf-confirm
connect
status
start
pause
disconnect
```

SSID/IPを確認し、PCへARP/ping/UDP/NRCVが届くか、Duty待機とSPIエラーを分けてログにする。映像clip不足やElectronの復号問題をHSPI故障と混同しない。

## Mainへの取り込み

ホスト試験だけではmainの実機検証済み条件を満たさない。少なくとも実ESP/Armビルド、HSPI 5 MHz、loopbackを確認する。映像経路の合格はAP接続と受信側照合を伴わせる。

合格したcommitだけをmainへ通常mergeし、mainの成果物を再ビルド・ESPへ再flashして回帰確認する。mainがdirtyなら勝手にstash/resetせず報告する。遠隔操作できていない機器やbranchを操作済みと書かない。

記録するもの: コミット、ESP/SX bin SHA、toolchain版、USB by-id、実GPIO、SW1、BDF SHA、SPI周波数、diag/loopback結果、RF試験時間・結果、未検証事項。flash操作はUSBデバイスを一意に識別してから行う。
