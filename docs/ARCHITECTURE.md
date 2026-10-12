# アーキテクチャ

## 全体像

```
Premium Apps(純正 RSS)  ──▶  hdrelay(127.0.0.1:8810)  ──TLS 1.3──▶  www.youtube.com / *.googlevideo.com / i.ytimg.com
        ▲                          │
        │ RSS 2.0 / HLS / JPEG     └─ 映像 TS + AAC 音声 → MPEG-TS に多重化
   純正 DvdPlayer が再生(HDMI)
```

純正ファームウェアには次の部品があり、本プロジェクトはそれらを**そのまま利用**します。

| 純正の部品 | 利用のしかた |
|---|---|
| Premium Apps のメニュー(`menu.rss`、RSS 方言のスクリプト) | タイルを 1 つ追加(bind mount でパッチ版に差し替え) |
| 純正 YouTube 画面(`youtube_menu.rss`) | 画面(サムネイル付き一覧・検索入力)をそのまま使い、`<submenu>` の接続先だけを `hdrelay` に変更 |
| 汎用 RSS プロバイダ(`CRSSApplication`) | RSS の `<link>` に **`http://` の URL を直接**渡せる。項目の `<enclosure>` が再生対象になる |
| DvdPlayer の HLS 再生 | `m3u8` と TS セグメントを HTTP で取得して再生(HTTP のみ。TLS は話せない) |

## `hdrelay` の HTTP エンドポイント

| パス | 内容 |
|---|---|
| `/search?q=…` | 検索結果の RSS 2.0(タイトル、長さ、サムネイル、`<enclosure>` → `/v/<id>.m3u8`)。長い動画は除外 |
| `/home.rss` | 固定キーワード("popular music")の検索結果 |
| `/v/<id>.m3u8` | VOD の HLS プレイリスト(映像+音声の多重化済み、`#EXT-X-ENDLIST`、セグメントは相対 URL) |
| `/v/<id>/<n>.ts` | n 番目のセグメント(映像 TS と音声 AAC を多重化したもの)。`Range` 対応 |
| `/t/<id>.jpg` | サムネイル(`i.ytimg.com` を TLS で取得して HTTP で返す) |
| `/set?h=480&max=20` | 実行時設定(**ループバックからのみ**) |
| `/health` | 生存確認(`ok`) |

HTTP/1.1 の keep-alive と `Range`(`206`)に対応。接続は同時 12 本まで。

## YouTube 側の手順

1. **visitorData の取得**: WEB クライアントで検索を 1 回投げ、レスポンスの `responseContext.visitorData` を得ます(1 時間キャッシュ)。
2. **検索**: InnerTube `POST /youtubei/v1/search`(WEB)。結果の解釈は MeeTube の `videolistparser` を使用。
3. **再生情報**: InnerTube `POST /youtubei/v1/player`(クライアント **VISIONOS** 1.02、visitorData つき)。`playerparser` が `hlsManifestUrl` を取り出します。
4. **HLS の選択**: マスタープレイリストから `avc1`+`mp4a` を含み、希望の高さ(既定 480)に最も近いバリアントを選びます。音声は別ファイル(`#EXT-X-MEDIA:TYPE=AUDIO`、ID3+ADTS の生 AAC)で、**すべてのバリアントが音声を別レンディションとして参照**します。
5. **セグメント取得と多重化**: 映像 TS と音声セグメントを取得し、1 本の TS に作り直します(下記)。

通信先は許可リスト(`www.youtube.com` への POST、`*.googlevideo.com`/`i.ytimg.com` への GET)のみ。リダイレクトも 1 ホップごとに検査し、署名付き URL はログに出しません。

## 映像 TS + AAC 音声 → 1 本の TS(`mux` 関数)

純正プレーヤーの HLS 実装は `#EXT-X-MEDIA`(別音声)に対応しておらず(バイナリ中にタグの文字列がありません)、音声が別ファイルの HLS をそのまま渡せません。
そこで 1 セグメントごとに次の変換をします(状態を持たない処理です)。

- 映像 TS から **H.264 の PID** のパケットだけを取り、各パケットに「所属する PES の DTS(なければ PTS)」を並べ替えキーとして付けます。
- 音声セグメント先頭の **ID3 `PRIV`(`com.apple.streaming.transportStreamTimestamp`)** から 90 kHz の基準 PTS を読み、
  ADTS フレーム(1024 サンプル)ごとに PTS を計算して、最大 6 フレームずつ PES(`stream_id=0xC0`)にし、PID 257 の TS パケットに分割します。
- **PAT/PMT を作り直し**(H.264 + AAC-ADTS、CRC32/MPEG)、映像と音声のパケットを PTS 順にインターリーブします。

検証: `ffprobe` で h264 + aac、`ffmpeg -f null` で隣接セグメントを連結してもエラー/警告なし、音声 PTS は境界でも連続。
`reference/python/tsmux.py` に同じ処理の Python 参考実装があります。

## キャッシュと先読み

- セグメントは LRU で最大 10 MiB。次のセグメントを先読みし、検索結果の先頭 3 件はプレイリストと最初のセグメントを先に用意します(クリックから再生までの待ちを短くするため)。
- 多重化は 1 つずつ(CPU/RAM が小さいため)。

## 採用しなかった方式(調査の結果)

| 方式 | 結果 |
|---|---|
| 純正の内蔵ブラウザ(YouTube XL、QtWebKit 4.7.1)で再生 | HTML5 `<video>` は純正エンジンにつながり再生できるが、**終了/再入場で DvdPlayer が落ちる/固まる**。HTTPS は不可(古い TLS)。 |
| ANDROID / ANDROID_VR の itag 18(統合 MP4) | 先頭付近の Range は取れるが、約 1.7 MiB 以降の Range が 403(2026 年 10 月時点の実測)。 |
| 端末に PC/外部サーバーを置く | 目的に反するため不採用(研究中のホスト試作では使用)。 |
