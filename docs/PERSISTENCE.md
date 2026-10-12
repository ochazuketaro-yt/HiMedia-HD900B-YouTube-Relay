# 起動時の自動実行(永続化)について

**結論: 書き込み可能な領域には「起動時に自動実行される場所」が見つかりませんでした。** そのため `hdrelay` は電源投入後に手動で
`sh /data/hd/boot.sh` を実行して起動します(`/data/hd` のファイルは再起動後も残ります)。

## 調べたこと(HD900B)

起動の流れ: Android の `init`(RAM 上の `/init.rc`)→ `/system/etc/init.venus.sh` → `/system/rtk_rootfs/etc/init.d/rcS`(`S??*` を順に実行)→ `RootApp DvdPlayer` と `HiMediaDMA`。

| 領域 | 書き込み | 再起動後 | 起動時に実行されるか |
|---|---|---|---|
| `/data`(mtd3、yaffs2、3.6 GB) | 可 | **残る** | されない |
| `/system`(mtd1、SquashFS) | 不可 | 元のまま | 起動スクリプトはここにある |
| `/`(initramfs)、`/tmp`(tmpfs) | 可 | **消える** | — |

- `/data` の `passwd`/`group`/`hosts`/`profile`/`ld.so.conf` は起動時にコピー元から補われますが、起動時に**実行**されるものはありません(`profile` はログインシェルが読むだけ)。
- DvdPlayer は起動時に `execute_script("run_tail")` を `system()` で呼びます。探す順序は `/etc/dvdplayer/script/<名前>` →(無い場合のみ)`/usr/local/etc/dvdplayer/script/<名前>`(= `/data/dvdplayer/script/`、書き込み可)ですが、
  純正に `run_tail` が存在するため `/data` 側は使われません。
- Android 側は `ro.secure=0`・`ro.debuggable=1` で adbd が root 動作しますが、`init.rc` に `/data` 上のスクリプトを実行する仕組みはありません。
- `himedia_diyconfig.dat`(`diy=true`)を読む実行ファイルは見つかりませんでした。

## 選択肢

1. **(現状)電源投入後に `sh /data/hd/boot.sh`** — 最も安全。ファームウェアは一切書き換えません。
2. **システム領域(mtd1 の SquashFS)を 1 回だけ書き換え**、`init.venus.sh` に `[ -x /data/hd/boot.sh ] && /data/hd/boot.sh &` を追加する。
   以後の更新は `/data/hd` だけで済みます。ただし**フラッシュの書き換えになり、失敗すると起動不能**になり得ます。
   手順の想定: ① `/dev/mtd/mtd1` を吸い出して退避 ② `unsquashfs` → 編集 → `mksquashfs`(圧縮方式・ブロックサイズは元に合わせる)
   ③ MEMERASE の ioctl と `write` だけの小さなツールで書き込み(端末に `flash_erase`/`nandwrite` はありません) ④ 電源再投入。
   起動時のイメージ検証の有無や SquashFS の詳細は未確認です。**このプロジェクトでは実施していません。**
3. Android 側のアプリ(`BOOT_COMPLETED` で起動)として常駐させる — 未検討/未検証。

## なぜ自動起動を同梱しないか

フラッシュを書き換える操作は、端末ごとの差(パーティション構成、検証の有無)が大きく、失敗時の影響が重大です。
復旧手段(ブートローダー経由のファームウェア書き戻し)を持つ方が**自己責任で**行う前提の話として、上記の手順案だけを記載しています。
