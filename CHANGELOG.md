# Changelog

## 0.1.0 — 初回公開

- HD900B(Realtek RTD1186、Linux 2.6.34)で動作確認。
- Premium Apps に YouTube タイルを追加。検索、HOME、純正プレーヤーでの再生(H.264 + AAC、既定 480p)。
- 端末内で動く中継 `hdrelay`(C++ 静的バイナリ、libcurl + OpenSSL 3.5、TLS 1.3)。
- 旧カーネル向け互換層(`statx` / `prlimit64` / time64 の不整合の修正)。
- インストーラ(`tools/install.py`)、アンインストーラ、DvdPlayer 復旧スクリプト。
- 既知の制限は README を参照(自動起動なし、長い動画は非表示、ライブ非対応 など)。
