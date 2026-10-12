# NOTICE — サードパーティ・ソフトウェアとライセンス

`hd900b-youtube-relay` 自体は **GNU General Public License v3**(`LICENSE`)で配布されます。
理由: MeeTube のパーサ(GPL-3.0)のソースコードを `hdrelay` に組み込んでいるため、全体が GPL-3.0 の結合物になります。
`dist/hdrelay` に含まれる(静的リンクされた)各コンポーネントのライセンスは次のとおりです。ライセンス全文は `licenses/` にあります。

| コンポーネント | バージョン | ライセンス | 入手先 / 備考 |
|---|---|---|---|
| hdrelay(本リポジトリの `src/relay`、`src/compat`、`src/shim`) | — | GPL-3.0-or-later(全体としては GPL-3.0) | 本リポジトリ |
| MeeTube のパーサ | commit `624b900` | **GPL-3.0** | https://github.com/IarChep/MeeTube — `src/vendor/meetube/` に無改変で収録、作者 IarChep |
| Glaze | commit `0a21fd9` | MIT | https://github.com/stephenberry/glaze — `src/vendor/glaze/` に収録(`licenses/Glaze-MIT.txt`) |
| OpenSSL | 3.5.9 | Apache-2.0 | https://www.openssl.org/ — `licenses/OpenSSL-Apache-2.0.txt`。ソースはビルド時に取得(`build/config.sh`) |
| libcurl | 8.22.0 | curl license(MIT/X 風) | https://curl.se/ — `licenses/curl-COPYING.txt` |
| zlib | 1.3.2 | zlib license | https://zlib.net/ — `licenses/zlib-LICENSE.txt` |
| uClibc-ng(C ライブラリ) | 1.0.50 | **LGPL-2.1** | https://uclibc-ng.org/ — Bootlin の GCC 13.3 ツールチェーンに同梱。`licenses/LGPL-2.1.txt` |
| libstdc++ / libgcc / libatomic | GCC 13.3.0 | GPL-3.0 + **GCC Runtime Library Exception 3.1** | https://www.gnu.org/licenses/gcc-exception-3.1.html — 例外条項により、これらをリンクしたプログラムのライセンスに追加の制約は生じません |

## 配布していないもの(必要に応じて実行時に入手)

- **Mozilla CA 証明書バンドル**(MPL-2.0): `tools/install.py` がインストール時に https://curl.se/ca/cacert.pem を取得(SHA-256 検証付き)します。リポジトリには含めていません。
- **ファームウェアの `menu.rss` / `youtube_menu.rss`**: メーカー/Realtek のファームウェアの一部であり、再配布しません。
  インストーラが**お使いの端末から**読み出し、手元でパッチを当てます(`tools/patch_menu.py`)。
- **Bootlin のクロスツールチェーン**: `build/fetch.sh` が公式サイトから取得します(再配布しません)。

## LGPL-2.1(uClibc-ng)について — 静的リンクと再リンクの権利

`dist/hdrelay` には uClibc-ng が静的にリンクされています。LGPL-2.1 §6 に従い、利用者が改変版の uClibc-ng で再リンクできるよう、
`hdrelay` の**完全なソースコードとビルド手順**(`src/`、`build/`)を公開しています。別の C ライブラリ/別バージョンで
再リンクしたい場合は `build/build.sh` のコンパイラを差し替えてください(`docs/BUILD.md`)。
uClibc-ng のソースは上記サイトまたは Bootlin のビルドレシピから入手できます。

## 参考にしたプロジェクト(コードは含みません)

- **yt-dlp**(The Unlicense, https://github.com/yt-dlp/yt-dlp): InnerTube クライアントの識別値(`VISIONOS` の version / User-Agent など)の確認に参照しました。
- **Lavalink youtube-source** の issue 議論: Range 取得や 403 に関する知見の確認に参照しました。

## 商標・非関与

YouTube は Google LLC の商標です。本プロジェクトは Google / YouTube、HiMedia、Realtek のいずれとも提携・承認関係になく、
各社の公式な製品・サービスではありません。YouTube の非公開 API(InnerTube)を利用するため、いつでも動作しなくなる可能性があり、
利用規約への適合は利用者の責任です(README の「免責」を参照)。
