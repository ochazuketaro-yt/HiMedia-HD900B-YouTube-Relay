# 技術メモ(ハマりどころの記録)

古いカーネル(Linux 2.6.34)に、新しい GCC 13 + uClibc-ng 1.0.50 の静的バイナリ、OpenSSL 3.5、libcurl を載せるときに踏んだ問題と対処です。
同じ系統(Realtek RTD1186 / Linux 2.6.34)の端末や、古いカーネル向けの静的ビルド全般で役立つ可能性があります。**いずれも HD900B 実機で確認した内容**です。

## 1. uClibc-ng 1.0.50 が新しい syscall を使う(`statx` / `prlimit64`)

- Bootlin の uClibc-ng では `stat`/`lstat`/`fstat`/`stat64`/`lstat64`/`fstatat` が **`statx`(syscall 4366)**、`getrlimit`/`setrlimit`/`prlimit` が **`prlimit64`(4338)** を使い、**ENOSYS のフォールバックがありません**。
  Linux 2.6.34 にはどちらも無く、呼び出しは `ENOSYS`(MIPS では errno **89**)で失敗します。`opendir` が動くのは、内部で古い `fstat64`(4215)を使っているためです。
- 対処: `src/compat/hd900b_compat.c` が同名の関数と `__GI_*` 別名を再実装(旧 syscall 4213/4214/4215/4293/4075/4076 + カーネル `struct stat64` → libc 構造体の変換)。**オブジェクトを `libc.a` より前に置く**と、`libc.a` の該当メンバーがリンクされず、libc 内部の呼び出し(`glob`/`ftw`/`sysconf`/`pthread` 初期化など)も新しい実装に解決されます(`--wrap` は不要)。
- 確認: `tools/check_newsyscalls.sh` で最終 ELF に `li v0,4366/4338/4353` が残っていないかを静的に検査。端末では `tests/compat_selftest`。

## 2. uClibc-ng の `time_t` が 64 ビットなのに、syscall は 32 ビットのまま

- `time_t` が 64 ビット(`struct timespec = {int64 tv_sec; long tv_nsec}`)ですが、`clock_gettime()` はユーザーの構造体をそのまま旧カーネルの 32 ビット版 syscall に渡します。
  カーネルは先頭 8 バイトに `{sec, nsec}` を書くので、**`tv_sec` の上位 32 ビットに `tv_nsec` が入り、`tv_nsec` は不定値**になります。
  `time()` と `gettimeofday()` は `clock_gettime()` 経由なので同様に壊れます。
- 症状: OpenSSL が証明書の検証で `format error in certificate's notBefore field (13)` を出す(`gmtime_r(time(NULL))` が西暦 11 億年台になる)。curl の計時も異常値になる。
  `nanosleep`/`select` の相対時間は小数部が落ちる(`usleep(1000)` が即時復帰)。
- 対処: `clock_gettime/getres/settime`、`gettimeofday`、`time`、`nanosleep`(+`__nanosleep_nocancel`)、`clock_nanosleep`、`select` を、32 ビット⇄64 ビットの変換つきで再実装。
- 未対応(libc 内部のインライン syscall): `pthread_cond_timedwait`、`pthread_mutex_timedlock`、`sem_timedwait`、`pselect`、`ppoll`、`timerfd`、`utimensat`。時限待ちに頼らない設計にしてください(`hdrelay` は `poll()` と `select()` だけを使います)。

## 3. 静的リンクした C++ で `std::thread` が NULL 関数を呼んで落ちる

- この uClibc-ng では **`libpthread.a` が 8 バイトの空スタブ**で、pthread の実体は `libc.a` の中にあります。
  静的リンクすると libstdc++ の弱参照(`pthread_detach` など)が未解決のまま NULL になり、`std::thread::detach()` が **epc=0** で SIGSEGV します。
- 対処: 必要な `pthread_*` を `-Wl,-u,pthread_xxx` で強制的にリンク(`build/build.sh` に一覧があります)。

## 4. OpenSSL のビルド

- `linux-mips32 no-asm no-shared no-dso …`。64 ビットのアトミック演算に **`-latomic`**(`libatomic.a`)が必要です。
- 端末上で TLS 1.3(`X25519MLKEM768`、`TLS_AES_256_GCM_SHA384`)が成立し、`www.youtube.com` へ約 0.4 秒でハンドシェイクできます。
- OpenSSL 3.0 LTS は 2026 年 9 月にサポート終了のため、3.5 LTS(〜2030 年 4 月)を使用しています。

## 5. libcurl / libtool の落とし穴

- `libtool` の `-static` は「静的ライブラリを優先」を意味するだけで、完全静的にはなりません。実行ファイル(curl コマンドなど)を作るときは **`-all-static`** が必要です。
- ホストの `pkg-config` が日本語などの非 ASCII パスを壊し、OpenSSL を検出できなくなりました。`PKG_CONFIG=/bin/false` で無効化しています。

## 6. DvdPlayer(純正プレーヤー)の HLS について

- 対応タグは `#EXTM3U #EXTINF #EXT-X-STREAM-INF #EXT-X-KEY #EXT-X-MEDIA-SEQUENCE #EXT-X-TARGETDURATION #EXT-X-ENDLIST`(バイナリ中の文字列から)。`#EXT-X-MEDIA`(別音声)、`#EXT-X-MAP`(fMP4)、`#EXT-X-BYTERANGE` の文字列は**ありません**。→ 多重化 TS を渡す設計にしました。
- HTTP クライアントの UA は `RealtekVOD/1.0.0 (Linux)`。プレイリストとセグメントは Range なしの `GET`(keep-alive)で取得し、先頭のみ再取得が数回あります。
- **非常に長いプレイリスト(1773 セグメント、61 KB、約 2.5 時間)を渡すと、セグメントを 1 つも要求しないまま UI が固まりました**。そのため一覧に出す動画の長さを既定 20 分に制限しています(原因の断定はしていません)。
- 固まった/落ちたときの復旧は `docs/TROUBLESHOOTING.md`。

## 7. YouTube 側の観測(2026 年 10 月、ホスト PC と端末で測定)

- **VISIONOS 1.02**(`clientName=VISIONOS`、`osVersion=26.5.23O471`): PO token なしで HLS マスターが取れ、映像 TS・音声(ID3+ADTS)とも 200。暗号化なし、`#EXT-X-MAP` なし。16 バリアント、全て音声は別レンディション。
- **ANDROID_VR 1.65.10**(yt-dlp も 2026-08 に既定から除外): itag 18 は先頭付近の Range のみ成功し、約 1.7 MiB を超える範囲は 403。
- **ANDROID**(20.10.38 / yt-dlp 21.26.364): itag 18 の先頭・中間・末尾の 1 KiB Range は成功したが、連続取得は ANDROID_VR と同様に途中で 403 になる傾向。
- 上記は時期・IP・動画によって変わる可能性があり、将来の動作を保証しません。
