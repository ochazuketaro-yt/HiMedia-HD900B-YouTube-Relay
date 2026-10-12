# hd900b-youtube-relay

**2011 年発売のメディアプレーヤー HiMedia HD900B(Realtek RTD1186)の純正「Premium Apps」から、現行の YouTube を検索・再生できるようにする小さな中継デーモンと、そのインストーラです。**

純正ファームウェアの YouTube 機能は、旧 YouTube Data API v2 / 旧 Flash 前提で、現在は動作しません。
このプロジェクトは、プレーヤー**自身の中**で動く中継(`hdrelay`)を足すことで、純正の画面・リモコン操作・純正プレーヤー(DvdPlayer)をそのまま使って、
現行の YouTube の動画を再生します。**PC やクラウドのサーバーは不要**です(インストール時にPCを使うだけ)。

> ⚠️ **非公式・自己責任のプロジェクトです。** YouTube の非公開 API を使うため、YouTube 側の変更でいつでも動かなくなる可能性があります。
> [免責](#免責)を必ずお読みください。

---

## 目次

- [できること](#できること)
- [動作確認環境](#動作確認環境)
- [他の RTD1186 搭載機での可能性](#他の-rtd1186-搭載機での可能性)
- [仕組み](#仕組み)
- [インストール](#インストール)
- [使い方](#使い方)
- [電源を入れ直したとき](#電源を入れ直したとき)
- [設定](#設定)
- [アンインストール](#アンインストール)
- [制限事項](#制限事項)
- [トラブルシューティング](#トラブルシューティング)
- [ソースからビルドする](#ソースからビルドする)
- [リポジトリの構成](#リポジトリの構成)
- [ライセンス](#ライセンス)
- [免責](#免責)
- [謝辞](#謝辞)

---

## できること

- Premium Apps に **YouTube アイコンのタイル**を追加し、純正の YouTube 画面(サムネイル付き一覧)から
  - **HOME**: 人気の音楽動画など(固定キーワードの検索結果)
  - **SEARCH**: リモコン/USB キーボードで入力した語で検索
  - 選んだ動画を**純正プレーヤーで再生**(映像 H.264、音声 AAC)
- 画質は既定で **480p**(`-H` で変更可)。音声も含め、純正プレーヤーの通常のHLS再生として扱われます。
- ファームウェア(フラッシュ)は**書き換えません**。追加するのは `/data/hd/` のファイルと、メニューの bind mount 1 つだけです。いつでも[元に戻せます](#アンインストール)。

## 動作確認環境

| 項目 | 内容 |
|---|---|
| 端末 | **HiMedia HD900B** |
| SoC | Realtek **RTD1186**(MIPS 24Kc、FPU 内蔵、RAM 約 470 MB) |
| OS | Linux 2.6.34-VENUS(`rtk_saturn` / Android 2.2.1 ベースの Realtek SDK ファームウェア、2011 年ビルド) |
| 接続 | 有線 LAN、Telnet(`root`、パスワード空)で操作可能なこと |
| PC(インストール時のみ) | Python 3.8 以降(標準ライブラリのみ)。Debian Linux で確認、macOS/Windows は未確認 |
| 確認した内容 | 検索、HOME、複数の動画(音楽ビデオ、短い動画)の再生、音声同期、画質 480p、電源再投入後の復元 |

動作確認は **HD900B 1台のみ**です。

## 他の RTD1186 搭載機での可能性

同じ Realtek の RTD1186 系 SoC を使うメディアプレーヤーは、**ファームウェア(Realtek SDK ベースの Linux 2.6.34 + 純正 DvdPlayer + RSS ベースの Premium Apps)がかなり共通している**ようです。
そのため、HD900B 以外の RTD1186 搭載機でも**動く可能性があります**。ただし、**私は HD900B でしか確認していません**(他機種は未検証)。

試すときの目安は [`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md) にまとめました(必要な条件、事前チェック、インストーラが拒否する場合の見方)。
動いた/動かなかった報告(機種名、ファームウェア版、`uname -a`)を Issue でいただけると助かります。

## 仕組み

```
 リモコン ─▶ 純正 Premium Apps ─▶ 純正 YouTube 画面(RSS)
                                        │  http://127.0.0.1:8810/ …(HOME / 検索 / サムネイル)
                                        ▼
            ┌──────────────────────── HD900B の中 ────────────────────────┐
            │  hdrelay (本プロジェクト、C++ 静的バイナリ)                      │
            │   ・検索 / プレーヤー情報の取得  ── TLS 1.3 ──▶ YouTube          │
            │   ・映像(TS) と 音声(AAC) を 1 本の MPEG-TS に多重化            │
            │   ・サムネイルを HTTP に変換                                    │
            └────────────────────────┬────────────────────────────────────┘
                                     │  http://127.0.0.1:8810/v/<id>.m3u8 (HLS)
                                     ▼
                       純正 DvdPlayer がデコードして HDMI へ出力
```

- 純正の内蔵ブラウザ/プレーヤーは古い TLS(OpenSSL 0.9.8)しか話せず、現行の YouTube に直接つながりません。そこで **TLS と YouTube とのやり取りだけを `hdrelay` が肩代わり**します。
- 取得方式は、MeeTube / yt-dlp の知見に基づき InnerTube の **VISIONOS クライアント**の HLS を使います(署名解読や PO token が不要なため)。
  HLS は「映像だけの TS」と「別ファイルの AAC 音声」に分かれているため、`hdrelay` が **1 セグメントずつ 1 本の TS に多重化**して純正プレーヤーに渡します。
- 詳しくは [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) と、ハマりどころの記録 [`docs/TECHNICAL-NOTES.md`](docs/TECHNICAL-NOTES.md)(古い uClibc-ng と Linux 2.6.34 の非互換など)を参照してください。

## インストール

必要なもの: HD900B と同じ LAN にある PC、Python 3.8 以降、本リポジトリ(`dist/hdrelay` を含む)。

1. HD900B の IP アドレスを確認します(例: `192.168.0.177`)。Telnet が有効で、`root`(パスワード空)でログインできる必要があります。
2. PC で次を実行します。

   ```sh
   git clone https://github.com/<YOUR-NAME>/hd900b-youtube-relay.git
   cd hd900b-youtube-relay
   python3 tools/install.py --host 192.168.0.177
   ```

   インストーラは次のことを行います。
   - 端末に Telnet でログインし、カーネル/CPU が想定どおりか確認します。
   - PC 上に一時的な HTTP サーバーを立て、端末の `wget` でファイルを受け渡します。
   - 端末の**純正 `menu.rss` / `youtube_menu.rss` を端末から読み出し**、PC 上でパッチを当てます(純正ファイルは再配布しません)。
   - `dist/hdrelay`、CA 証明書(https://curl.se/ca/cacert.pem、SHA-256 検証)、パッチ済み RSS、補助スクリプトを `/data/hd/` に置きます(SHA-256 で検証)。
   - `/data/hd/boot.sh` を実行し、中継を起動してメニューを差し替えます。
3. 「完了」と表示されたら、テレビで **ホーム → Premium Apps** を開きます。

> `--yes` で確認を省略、`--cacert FILE` で CA バンドルを指定、`--relay FILE` で自分でビルドしたバイナリを使えます。`--help` で全オプションが出ます。

`dist/hdrelay` の SHA-256 は `dist/SHA256SUMS` にあります。自分でビルドする場合は[ソースからビルドする](#ソースからビルドする)。

## 使い方

1. **Premium Apps** を開くと、**YouTube アイコンのタイル**(内部名 `HD900B_YT`)が増えています。
2. 開くと純正の YouTube 画面が出ます。
   - **HOME** … 人気の音楽動画などの一覧(固定の検索キーワード)。
   - **SEARCH** … 検索語を入力(画面のキーボード/USB キーボード)。一覧にサムネイルとタイトル、長さが出ます。
3. 動画を選ぶと再生が始まります(最初のデータが揃うまで数秒〜10 秒ほど)。
4. 再生後に一覧へ戻ると、純正の「前回の続きから再生しますか?」という確認が出ることがあります(純正プレーヤー側の既存機能です)。

## 電源を入れ直したとき

端末を再起動すると `/tmp` が消えるため、中継とメニューの差し替えも消えます(`/data/hd` のファイルは残ります)。
再起動後は Telnet で次の **1 コマンド**を実行してください(何度実行しても安全です)。

```sh
sh /data/hd/boot.sh
```

> ファームウェアの起動処理は読み取り専用領域にあり、書き込み可能な領域には「起動時に自動実行される場所」がありません。
> 完全な自動起動にはフラッシュ(システム領域)の書き換えが必要になるため、このプロジェクトでは**意図的に行っていません**。
> 詳細と検討内容は [`docs/PERSISTENCE.md`](docs/PERSISTENCE.md)。

## 設定

`hdrelay` は端末内で `127.0.0.1:8810` だけを待ち受けます(外部からは見えません)。設定は起動引数、または端末上での実行時変更です。

| 項目 | 既定 | 変更方法 |
|---|---|---|
| 画質(希望の高さ) | 480 | 起動引数 `-H 360` など。実行中は端末上で `wget -q -O - 'http://127.0.0.1:8810/set?h=360'` |
| 一覧に出す動画の長さの上限(分) | 20 | 起動引数 `-m 30`。実行中は `…/set?max=30`(下記の制限事項を参照) |
| ポート / 待受アドレス | 8810 / 127.0.0.1 | `-p`, `-b`(`device/boot.sh` を編集) |
| CA 証明書 | `/tmp/hd900b/tls/cacert.pem` | `-c` |
| ログ | `/tmp/hd900b/relay/relay.log` | `-l`(署名付き URL は記録しません) |

## アンインストール

端末上で:

```sh
sh /data/hd/uninstall.sh
```

中継を止め、メニューの差し替え(bind mount)を外し、`/data/hd` と `/tmp/hd900b` を削除します。ファームウェアは最初から書き換えていないため、これで完全に元の状態です(電源の入れ直しでも元に戻ります)。

## 制限事項

- **YouTube の仕様変更で動かなくなる可能性があります。** 特に 2026 年 8 月以降、各種の非公式クライアントで取得が制限されています(`docs/TECHNICAL-NOTES.md`)。本プロジェクトが現在使っている方式が将来も有効とは限りません。
- **長い動画は一覧に出しません(既定 20 分まで)。** 再生リストが約 1000 セグメントを超える動画(約 2 時間以上)で純正プレーヤーが停止した経験に基づく暫定値で、真の上限は測定していません。
- ライブ配信、年齢制限/ログインが必要な動画、字幕、コメント、再生リスト、ログイン、履歴には**対応していません**。検索は 1 ページ目のみです。
- HOME は YouTube の「ホーム」ではなく、固定キーワードの検索結果です。
- 画質は 360p〜1080p のうち、H.264 + AAC のバリアントから希望の高さに最も近いものを選びます。デコードの限界は 480p でのみ確認しています。
- **電源投入後の自動起動はありません**(上記)。
- 純正の内蔵ブラウザ(YouTube XL)経由の再生は、再入場時にプレーヤーが落ちる/固まるため採用していません。
- `pthread_cond_timedwait` などの時限待ちは、古い C ライブラリの都合で互換層の対象外です(本プログラムでは使っていません)。

## トラブルシューティング

詳しくは [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md)。主なもの:

| 症状 | 確認/対処 |
|---|---|
| タイルが出ない | 端末で `sh /data/hd/boot.sh`(再起動後は必須)。`grep menu.rss /proc/mounts` で bind を確認。Premium Apps を一度閉じて開き直す |
| 一覧が空/エラー | `wget -q -O - http://127.0.0.1:8810/health` が `ok` か。`/tmp/hd900b/relay/relay.log` を確認 |
| 再生が始まらない | 同ログに `ERR` が出ていないか。別の動画で試す(ライブ/長尺/制限付きは不可) |
| 画面操作が効かなくなった | 端末で DvdPlayer を再起動: `kill -9 <MAIN の pid>` → RootApp が自動で再起動します(`docs/TROUBLESHOOTING.md`) |
| インストーラが拒否する | カーネル/CPU が想定外。`docs/COMPATIBILITY.md` を参照 |

## ソースからビルドする

ホスト(Linux x86_64)で、Bootlin の GCC 13.3 クロスツールチェーン(MIPS32 little-endian / uClibc-ng)を使って、完全静的な実行ファイルを作ります。

```sh
build/build.sh              # ツールチェーン・zlib・OpenSSL・libcurl を取得(SHA-256 検証)してビルドし、dist/hdrelay を生成
```

必要なもの: `bash`, `make`, `perl`, `xz`, `tar`, ホストの `gcc`, `sha256sum`, `curl` または `wget` または `python3`。所要時間は 15〜30 分程度です。
手順の詳細・オプション・古い C ライブラリへの対処は [`docs/BUILD.md`](docs/BUILD.md)。
端末上の回帰テスト(互換層の自己診断)は `tests/` にあります。

## リポジトリの構成

```
hd900b-youtube-relay/
├── README.md                 このファイル
├── LICENSE                   GNU GPL v3
├── NOTICE.md                 サードパーティのライセンス/謝辞
├── dist/                     端末に入れるもの(ビルド済み hdrelay と SHA256SUMS)
├── device/                   端末側スクリプト(boot.sh / uninstall.sh)
├── tools/                    インストーラ(install.py / hdtelnet.py / patch_menu.py)と検査ツール
├── src/
│   ├── relay/hdrelay.cpp     中継デーモン本体
│   ├── compat/               古い Linux 2.6.34 + uClibc-ng 用の syscall 互換層(C)
│   ├── shim/qtshim.h         Qt 非依存でパーサをビルドするための小さな互換ヘッダ
│   └── vendor/               取り込んだ MeeTube のパーサ(GPL-3.0)と Glaze(MIT)
├── build/                    ビルドスクリプトと固定バージョン/ハッシュ(config.sh)
├── tests/                    端末上の互換層セルフテスト
├── reference/                参考実装(Python の TS 多重化)
├── docs/                     設計・技術メモ・ビルド・互換性・永続化・トラブルシューティング
└── licenses/                 ライセンス全文
```

## ライセンス

**GNU General Public License v3.0** です(`LICENSE`)。MeeTube(GPL-3.0)のパーサを組み込んでいるため、全体が GPL-3.0 になります。
OpenSSL(Apache-2.0)、libcurl、zlib、Glaze(MIT)、uClibc-ng(LGPL-2.1、静的リンク)、GCC ランタイム(例外付き GPL)を含みます。
一覧と再リンクの権利については [`NOTICE.md`](NOTICE.md) を参照してください。

## 免責

- 本ソフトウェアは**無保証**で提供されます。使用により生じたいかなる損害(端末の故障、データの損失、アカウントやネットワークに関する不利益を含む)についても、作者は責任を負いません。
- 本プロジェクトは **Google / YouTube、HiMedia、Realtek と無関係**で、承認も受けていません。YouTube は Google LLC の商標です。
- YouTube の非公開 API(InnerTube)を利用します。**YouTube の利用規約や各国の法令への適合は利用者の責任**です。個人的な利用の範囲で、著作権を尊重してお使いください。
- ファームウェアのフラッシュは書き換えませんが、端末のシェル(root)で `/data` にファイルを置き、メニューを一時的に差し替えます。
  操作を誤ると端末の設定を壊す可能性があります。事前に端末の復旧手段(ブートローダー経由のファームウェア復旧など)を確認しておくことを勧めます。
- 動画の著作権は各権利者に帰属します。

## 謝辞

- [MeeTube](https://github.com/IarChep/MeeTube)(IarChep) — Qt 非依存の InnerTube パーサ群(GPL-3.0)を利用させていただきました。
- [yt-dlp](https://github.com/yt-dlp/yt-dlp) と Lavalink youtube-source のコミュニティ — YouTube クライアント仕様の変化に関する知見。
- [Glaze](https://github.com/stephenberry/glaze)、[curl](https://curl.se/)、[OpenSSL](https://www.openssl.org/)、[zlib](https://zlib.net/)、[uClibc-ng](https://uclibc-ng.org/)、[Bootlin](https://toolchains.bootlin.com/) のツールチェーン。
