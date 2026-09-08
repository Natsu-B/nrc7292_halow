# Electron Receiver

全体の手順は `../README_ja.md`。このフォルダーだけを移すと `../assets` が失われ、内蔵デモとテストが動きません。ZIP全体を展開してください。

Windows開発PCでNode.js22とネットワーク接続を用意し、PowerShellで `./build_windows.ps1`。完成したportable exeを受信PCへ配布します。受信PCにNode、Python、ffplay、GStreamer等の追加インストールは不要な構成です。

**この配布物にexeは含まれていません。** Electron本体起動・H264 WebCodecs・Windows packagingは未検証です。Node UDP/フレーム復元と同じrendererのMJPEG表示は検査済みです。

待受UDP50000、ESP送信元192.168.50.2、PCのEthernet192.168.50.10/24が例です。AP-100AHには純正FWを使い、PCへ特別なHaLowドライバーは入れません。自動的にファイアウォールを変更しません。

記録は完全フレームのraw H264またはMJPEG。MP4ではありません。NRCVは独自形式で、RTP/RTSPは受信しません。インターネットアクセス、外部ページ、Nodeのrenderer公開を使わず、プリロード経由の限定IPCだけで操作します。
