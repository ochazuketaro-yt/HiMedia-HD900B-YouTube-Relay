# reference/

`python/tsmux.py` は、`hdrelay`(C++)が 1 セグメントごとに行う「映像 TS + AAC 音声 → 1 本の MPEG-TS」の多重化の
**Python 参考実装**です。ホスト PC で `ffprobe` / `ffmpeg` を使って出力を検証するときや、挙動の確認に使えます。
