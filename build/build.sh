#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build dist/hdrelay (static MIPS32r2 o32 executable for the HD900B) from source.
#
#   build/build.sh                      # fetch everything, build deps + relay
#   DL_DIR=~/dl build/build.sh          # reuse already downloaded tarballs
#   TOOLCHAIN_DIR=/opt/mips32el--uclibc--stable-2024.05-1 build/build.sh   # reuse an extracted toolchain
#
# Host needs: bash, make, perl (OpenSSL), xz, tar, gcc (host), sha256sum.  No root, no network after the fetch.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(dirname "$HERE")
# shellcheck source=config.sh
. "$HERE/config.sh"
DL_DIR=${DL_DIR:-$HERE/dl}
WORK=${WORK:-$HERE/work}
PREFIX=$WORK/install
JOBS=${JOBS:-$(nproc)}
export DL_DIR
mkdir -p "$WORK" "$PREFIX"
# Keep build-machine paths out of the binaries (assert strings, OPENSSLDIR, __FILE__).
HD_CFLAGS="$HD_CFLAGS -ffile-prefix-map=$ROOT=/src -ffile-prefix-map=$WORK=/build"

# ---------------------------------------------------------------------------------- inputs
export TOOLCHAIN_DIR=${TOOLCHAIN_DIR:-}
"$HERE/fetch.sh"
if [ -z "$TOOLCHAIN_DIR" ]; then
  TOOLCHAIN_DIR=$WORK/$TOOLCHAIN_NAME
  [ -d "$TOOLCHAIN_DIR" ] || tar -C "$WORK" -xf "$DL_DIR/${TOOLCHAIN_NAME}.tar.xz"
fi
export PATH=$TOOLCHAIN_DIR/bin:$PATH
export CC=mipsel-linux-gcc CXX=mipsel-linux-g++ AR=mipsel-linux-ar RANLIB=mipsel-linux-ranlib STRIP=mipsel-linux-strip
command -v "$CC" >/dev/null || { echo "cross compiler not found in $TOOLCHAIN_DIR/bin" >&2; exit 1; }
"$CC" --version | head -1

unpack() { [ -d "$WORK/src/$2" ] || { mkdir -p "$WORK/src"; tar -C "$WORK/src" -xf "$DL_DIR/$1"; }; }
unpack "zlib-${ZLIB_VER}.tar.gz"   "zlib-${ZLIB_VER}"
unpack "openssl-${OPENSSL_VER}.tar.gz" "openssl-${OPENSSL_VER}"
unpack "curl-${CURL_VER}.tar.xz"   "curl-${CURL_VER}"

# ---------------------------------------------------------------------------------- zlib
if [ ! -f "$PREFIX/lib/libz.a" ]; then
  echo "== zlib $ZLIB_VER"; mkdir -p "$WORK/b/zlib"; ( cd "$WORK/b/zlib"
  CFLAGS="$HD_CFLAGS" "$WORK/src/zlib-${ZLIB_VER}/configure" --static --prefix="$PREFIX" >configure.log 2>&1
  make -j"$JOBS" >make.log 2>&1; make install >>make.log 2>&1 ); fi

# ---------------------------------------------------------------------------------- OpenSSL (libs only)
if [ ! -f "$PREFIX/lib/libssl.a" ]; then
  echo "== OpenSSL $OPENSSL_VER (about 10-15 minutes)"; mkdir -p "$WORK/b/openssl" "$WORK/wrap"
  # OpenSSL records its compiler command line in the library ("compiler: ..."), so the path-remapping flags are injected
  # through a wrapper instead of the Configure arguments (keeps build-machine paths out of the binary).
  printf '#!/bin/sh\nexec "%s/mipsel-linux-gcc" -ffile-prefix-map=%s=/src -ffile-prefix-map=%s=/build "$@"\n' "$TOOLCHAIN_DIR/bin" "$ROOT" "$WORK" > "$WORK/wrap/mipsel-linux-gcc"
  chmod +x "$WORK/wrap/mipsel-linux-gcc"
  ( export PATH="$WORK/wrap:$PATH"; cd "$WORK/b/openssl"
  "$WORK/src/openssl-${OPENSSL_VER}/Configure" linux-mips32 --prefix=/usr/local/hd900b --openssldir=/etc/ssl \
      no-asm no-shared no-dso no-tests no-docs no-engine no-ssl3 no-comp no-afalgeng no-apps \
      CC="$CC" AR="$AR" RANLIB="$RANLIB" -march=mips32r2 -mtune=24kc -mfp32 -mhard-float -fno-pie >configure.log 2>&1
  make -j"$JOBS" build_libs >make.log 2>&1
  # install into a staging root, then merge into $PREFIX (the install prefix above is only what gets compiled into the library)
  make install_dev DESTDIR="$WORK/stage" >>make.log 2>&1
  cp -a "$WORK/stage/usr/local/hd900b/include/." "$PREFIX/include/"; mkdir -p "$PREFIX/lib"; cp -a "$WORK/stage/usr/local/hd900b/lib/." "$PREFIX/lib/" ); fi

# ---------------------------------------------------------------------------------- libcurl (library only; HTTP/HTTPS)
# PKG_CONFIG=/bin/false: configure must not pick up host pkg-config data (and chokes on non-ASCII paths).
# -latomic: OpenSSL uses 64-bit atomics, which 32-bit MIPS provides through libatomic.
if [ ! -f "$PREFIX/lib/libcurl.a" ]; then
  echo "== libcurl $CURL_VER"; mkdir -p "$WORK/b/curl"; ( cd "$WORK/b/curl"
  PKG_CONFIG=/bin/false CFLAGS="$HD_CFLAGS" LDFLAGS="-L$PREFIX/lib" CPPFLAGS="-I$PREFIX/include" LIBS="-lpthread -latomic" \
  "$WORK/src/curl-${CURL_VER}/configure" --host=mipsel-linux --prefix="$PREFIX" --disable-shared --enable-static \
      --with-openssl="$PREFIX" --with-zlib="$PREFIX" --without-libpsl --without-libidn2 --without-nghttp2 --without-nghttp3 \
      --without-ngtcp2 --without-brotli --without-zstd --disable-ldap --disable-ldaps --disable-rtsp --disable-dict \
      --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-mqtt \
      --disable-smb --disable-ftp --disable-file --disable-manual --disable-docs --disable-threaded-resolver --disable-ipv6 \
      >configure.log 2>&1
  make -C lib -j"$JOBS" >make.log 2>&1; make -C lib install >>make.log 2>&1; make -C include install >>make.log 2>&1 ); fi

# ---------------------------------------------------------------------------------- compat layer + relay
echo "== hd900b_compat + hdrelay"
mkdir -p "$ROOT/dist" "$WORK/o"
"$CC" $HD_CFLAGS -no-pie -Wall -Wextra -Wno-missing-attributes -Wno-nonnull-compare -c "$ROOT/src/compat/hd900b_compat.c" -o "$WORK/o/hd900b_compat.o"
V=$ROOT/src/vendor
PTHREAD_U=""
for f in pthread_once pthread_getspecific pthread_setspecific pthread_create pthread_join pthread_equal pthread_self pthread_detach \
         pthread_cancel pthread_mutex_lock pthread_mutex_trylock pthread_mutex_timedlock pthread_mutex_unlock pthread_mutex_init \
         pthread_mutex_destroy pthread_cond_broadcast pthread_cond_signal pthread_cond_wait pthread_cond_timedwait pthread_cond_init \
         pthread_cond_destroy pthread_key_create pthread_key_delete pthread_mutexattr_init pthread_mutexattr_settype \
         pthread_mutexattr_destroy pthread_attr_init pthread_attr_destroy pthread_attr_setdetachstate pthread_attr_setstacksize \
         pthread_sigmask pthread_kill; do PTHREAD_U="$PTHREAD_U -Wl,-u,$f"; done
# uClibc-ng keeps pthreads inside libc.a (libpthread.a is an empty stub); with static linking libstdc++'s weak
# references would stay NULL (std::thread::detach() -> SIGSEGV) unless the symbols are forced in.
"$CXX" -std=c++23 -O2 $HD_CFLAGS -no-pie -static \
  -include "$ROOT/src/shim/qtshim.h" -I"$ROOT/src/shim" -I"$V/meetube" -I"$V/meetube/parsers" -I"$V/meetube/types" -I"$V/glaze" -I"$PREFIX/include" \
  "$ROOT/src/relay/hdrelay.cpp" "$V/meetube/parsers/playerparser.cpp" "$V/meetube/parsers/videolistparser.cpp" "$V/meetube/parsers/continuation.cpp" \
  "$WORK/o/hd900b_compat.o" -o "$WORK/hdrelay.unstripped" \
  -L"$PREFIX/lib" -lcurl -lssl -lcrypto -lz -lpthread -latomic -Wl,-u,__xstat64_conv $PTHREAD_U
"$STRIP" -o "$ROOT/dist/hdrelay" "$WORK/hdrelay.unstripped"
( cd "$ROOT/dist" && sha256sum hdrelay > SHA256SUMS && cat SHA256SUMS )
"$ROOT/tools/check_newsyscalls.sh" "$WORK/hdrelay.unstripped" || true
echo "done: $ROOT/dist/hdrelay"
