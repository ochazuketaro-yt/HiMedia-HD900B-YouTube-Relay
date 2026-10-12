# ビルド手順

## 必要なもの(ホスト)

Linux x86_64(Debian 13 で確認)。`bash`, `make`, `perl`(OpenSSL の Configure)、`xz`, `tar`, ホストの `gcc`, `sha256sum`、ダウンロード用に `curl` か `wget` か `python3`。
ディスクは約 2 GB、時間は 15〜30 分(OpenSSL が大半)。ネットワークは取得時のみ。

## 一括ビルド

```sh
build/build.sh
```

`build/config.sh` に固定したバージョンと SHA-256 で次を取得し、`build/work/` にビルドします。

1. Bootlin の `mips32el--uclibc--stable-2024.05-1`(GCC 13.3.0、binutils 2.41、uClibc-ng 1.0.50、ヘッダ 4.19、ハードフロート)
2. zlib 1.3.2 → OpenSSL 3.5.9(`linux-mips32 no-asm no-shared …`)→ libcurl 8.22.0(ライブラリのみ、HTTP/HTTPS)
3. `src/compat/hd900b_compat.c` → `hd900b_compat.o`
4. `hdrelay`(`-std=c++23`、MeeTube のパーサ + Glaze + シム)を**完全静的**リンク → `dist/hdrelay`(strip 済み)と `dist/SHA256SUMS`

### 環境変数

| 変数 | 意味 |
|---|---|
| `DL_DIR` | ダウンロード済みの tarball の置き場所(既定 `build/dl`) |
| `TOOLCHAIN_DIR` | 展開済みのツールチェーンを使う(取得/展開を省略) |
| `WORK` | 作業ディレクトリ(既定 `build/work`) |
| `JOBS` | `make -j` の数 |

## ターゲットの CPU フラグ

`-march=mips32r2 -mtune=24kc -mfp32 -mhard-float -fno-pie -no-pie`(`build/config.sh` の `HD_CFLAGS`)。ELF のフラグが `0x70001007`(純正の DvdPlayer と同じ MIPS32r2 o32 / ハードフロート)になることを `readelf -h -A` で確認できます。
**soft-float にしないでください**: 端末の純正バイナリは hard-float で、実機に FPU(FIR の ProcessorID `0x93`)があり、命令の実行も確認済みです。

## リンクの要点(自分でリンクし直す場合)

- `hd900b_compat.o` を**他のオブジェクトと一緒に、`libc` より前**に置く。`-Wl,-u,__xstat64_conv` も付ける。
- `-static`(C++ の `std::thread` を使うなら `-Wl,-u,pthread_…` 一式。`build/build.sh` 参照)。
- OpenSSL を使うなら `-latomic`。libtool 経由のリンクは `-all-static`。
- 作った ELF に新しい syscall が残っていないか: `tools/check_newsyscalls.sh dist/hdrelay`
  (OpenSSL の FFC パラメータ生成関数にある定数 `4353` は誤検出で、無害です)。

## 互換層のセルフテスト

```sh
export PATH=<toolchain>/bin:$PATH
tests/build_selftest.sh              # tests/out/compat_selftest を生成
# 端末へコピーして実行(終了コード = 失敗数)
```

HD900B での結果: 全項目 PASS(stat/lstat/fstat/rlimit/glob/ftw/sysconf/getrandom、時刻と nanosleep/select/usleep の実時間、pthread + TLS)。

## LGPL(uClibc-ng)の再リンク

`dist/hdrelay` に静的にリンクされた uClibc-ng を差し替えたい場合は、`build/build.sh` のツールチェーンを別の uClibc-ng ビルドに替えて再実行してください(`TOOLCHAIN_DIR`)。互換層は uClibc-ng 1.0.50 の内部シンボル(`__GI_*`、`__xstat64_conv`)に依存するため、別バージョンでは調整が必要です。
