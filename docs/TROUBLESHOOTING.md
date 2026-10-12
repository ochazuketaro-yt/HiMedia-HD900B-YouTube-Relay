# トラブルシューティング

端末で Telnet(`root`、パスワード空)に入って確認します。ログは `/tmp/hd900b/relay/relay.log`(署名付き URL は記録されません)。

## 状態の確認

```sh
pidof hdrelay                                  # 中継が動いているか
wget -q -O - http://127.0.0.1:8810/health      # "ok" が返るか
grep menu.rss /proc/mounts                     # メニューの bind mount があるか(1 行)
cat /tmp/hd900b/relay/relay.log                # 直近のリクエストと ERR
date                                           # 日時が正しいか(TLS の証明書検証に必要)
```

## 症状別

| 症状 | 原因の候補 / 対処 |
|---|---|
| Premium Apps に YouTube タイルが無い | 再起動後は `sh /data/hd/boot.sh`。それでも無ければ、Premium Apps を一度閉じて開き直す。`grep menu.rss /proc/mounts` で bind を確認 |
| 一覧が出ない/空 | `health` が `ok` でない → `boot.sh`。`ok` なら `relay.log` に `ERR search`(HTTP 4xx/5xx は YouTube 側の変更の可能性)。`date` がずれていないか |
| サムネイルが出ない | `relay.log` の `/t/…` を確認(`i.ytimg.com` に到達できるか) |
| 選んでも再生が始まらない | `relay.log` の `ERR /v/…`(`not playable`、`no HLS`、`live streams are not supported` など)。別の動画で試す。ライブ、年齢制限、地域制限の動画は不可 |
| 再生が途中で止まる | ネットワーク/YouTube 側。同じ動画をもう一度。`/set?h=360` で画質を下げて試す |
| 音が出ない/映像がコマ落ち | HDMI 直結で確認してください(キャプチャボード経由だとプレーヤーが誤動作しているように見えることがあります) |
| リモコンも本体ボタンも効かない(画面は出ている) | DvdPlayer のハング。`sh /data/hd/recover_dvdplayer.sh` で再起動(10〜15 秒)。`/tmp` とメニューの bind は残ります |
| インストーラが「アンカーが見つからない」で止まる | 純正 `menu.rss` の構成が違う。`docs/COMPATIBILITY.md` |
| `TimeoutError` / 接続できない | Telnet が有効か、IP が合っているか、PC のファイアウォールが `--port`(既定 8899)を許可しているか |

## リセット

```sh
sh /data/hd/uninstall.sh      # 完全に元に戻す(ファームウェアは未変更)
```

電源を入れ直すだけでも `/tmp` と bind mount は消えます(`/data/hd` は残るので `boot.sh` で復元できます)。

## ログの取り方(Issue 用)

```sh
uname -a; cat /proc/cpuinfo | grep model; date
cat /tmp/hd900b/relay/relay.log
```
署名付き URL・Cookie・visitorData は出力されませんが、念のため貼る前に目を通してください。
