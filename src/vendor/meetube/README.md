# vendored: MeeTube parsers

このディレクトリのソースは [MeeTube](https://github.com/IarChep/MeeTube)(Nokia N9 向け YouTube クライアント、作者 IarChep)から
**無改変で**取り込んだ Qt 非依存の JSON パーサ群です。

| 項目 | 内容 |
|---|---|
| 取得元 | https://github.com/IarChep/MeeTube |
| コミット | `624b90008419f1dfb20e37cadd7a3b7bf701d570`(2026-07-20) |
| ライセンス | GNU GPL v3(同梱の `COPYING`) |
| 取り込んだファイル | `src/core/parsers/{playerparser,videolistparser,continuation}.{h,cpp}`、`ytjson.h`、`jsonscan.h`、`tokenscan.h`、`rendererparser.h`、`rendererinternal.h`、`src/core/types/servicedatatypes.h` |
| 用途 | 検索結果(`videolistparser`)と `/player` 応答(`playerparser`)の解釈 |

Qt を使わずにビルドするため、`src/shim/qtshim.h`(本リポジトリ独自の小さな互換ヘッダ)を `-include` しています。
MeeTube 本体(GUI、ネットワーク層、GStreamer 連携など)は取り込んでいません。

これらを組み込んだ `hdrelay` は GPL-3.0 で配布されます(ルートの `LICENSE`、`NOTICE.md` を参照)。
