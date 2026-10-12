# 互換性 — HD900B 以外の端末で試すとき

**動作確認は HiMedia HD900B(Realtek RTD1186)だけです。** 同じ Realtek の RTD1186 系 SoC 搭載機は、ファームウェア
(Realtek SDK の Linux 2.6.34 + 純正 DvdPlayer + RSS ベースの Premium Apps)がかなり共通しているようなので、動く可能性があります。
以下は「動くための条件」と「試す順番」です(未検証の機種についての推測を含みます)。

## 動くための条件

| # | 条件 | 確認コマンド(Telnet) | HD900B での値 |
|---|---|---|---|
| 1 | CPU が MIPS32r2 リトルエンディアン、ハードウェア FPU あり | `cat /proc/cpuinfo` | `MIPS 24Kc V8.5  FPU V0.0` |
| 2 | カーネルが Linux 2.6.34 系(旧 syscall 前提のため。新しければ互換層は不要だが害もない見込み) | `uname -a` | `2.6.34-VENUS` |
| 3 | root で Telnet できる | — | `root` / パスワード空 |
| 4 | `/data` が書き込み可能で永続 | `mount \| grep data`, `df /data` | yaffs2、空き約 3.6 GB |
| 5 | 純正の Premium Apps が RSS(`scripts/menu.rss`)で構成され、`youtube_menu.rss` がある | `ls /system/rtk_rootfs/usr/local/bin/scripts/` | あり |
| 6 | そのパスが読み取り専用でも、ファイルに対する `mount --bind` ができる | `mount --bind` | 可 |
| 7 | 端末に GNU `wget`(`--post-file` 対応)と `sha256sum` がある | `wget --help`, `sha256sum` | wget 1.12、BusyBox |
| 8 | DvdPlayer が HTTP の HLS(TS: H.264 + AAC)を再生できる | 下記の事前テスト | 再生可 |
| 9 | 端末が NTP などで正しい日時を持つ(TLS 証明書の検証に必要) | `date` | 正しい |
| 10 | LAN から外部の HTTPS(443)に出られ、DNS が引ける | — | 可 |

## 試す順番

1. **`tests/compat_selftest`**(`tests/build_selftest.sh` でビルド)を端末で実行し、全項目 PASS を確認します。
   FAIL があれば、そこが互換層の対象/想定外の差です(`docs/TECHNICAL-NOTES.md`)。
2. **HLS 再生の事前テスト**: PC で H.264 + AAC の小さな HLS(`ffmpeg -i in.mp4 -c copy -f hls -hls_time 10 -hls_playlist_type vod out/index.m3u8`)を作り、
   適当な HTTP サーバー(Range 対応が望ましい)で配り、`<enclosure url="http://PC/out/index.m3u8" type="application/vnd.apple.mpegurl">` を 1 項目だけ持つ RSS を `rss_file://` で開いて再生できるか確認します。
3. `python3 tools/install.py --host <IP>`。想定外のカーネルだとインストーラが確認を求めます(`--yes` で続行できますが、自己責任)。
4. 何かあれば `sh /data/hd/uninstall.sh` で完全に元に戻ります。

## 機種ごとに変わりそうな点

- Premium Apps のファイル構成/タイルの作りが違うと `tools/patch_menu.py` が「アンカーが見つからない」で止まります(推測で壊さないための仕様)。
- ハードウェアのデコード能力(H.264 のプロファイル/レベル、解像度)。HD900B では Main@L3.0 の 480p までを確認しています。`-H` で画質を下げて試せます。
- ファームウェアの HLS 実装の差(別音声、fMP4 の対応など)。対応していれば多重化は不要になりますが、本プロジェクトは多重化済み TS を前提にしています。

## 報告のお願い

動いた/動かなかった場合、次を Issue に書いていただけると助かります。

- 機種名、ファームウェアの版、`uname -a`、`cat /proc/cpuinfo`、`ls /system/rtk_rootfs/usr/local/bin/scripts/`
- `tests/compat_selftest` の結果
- インストーラの出力と `/tmp/hd900b/relay/relay.log`(署名付き URL は記録されません)
