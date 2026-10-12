#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Download (and SHA-256 verify) the toolchain and the third-party sources into $DL_DIR (default: build/dl).
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=config.sh
. "$HERE/config.sh"
DL_DIR=${DL_DIR:-$HERE/dl}
mkdir -p "$DL_DIR"

download() {   # download <sha256> <file> <url>...
  local sha=$1 file=$2; shift 2
  if [ -f "$DL_DIR/$file" ] && echo "$sha  $DL_DIR/$file" | sha256sum -c --status; then echo "ok (cached)  $file"; return; fi
  local url
  for url in "$@"; do
    echo "fetching $url"
    if command -v curl >/dev/null; then curl -fL --retry 3 -o "$DL_DIR/$file" "$url" || continue
    elif command -v wget >/dev/null; then wget -O "$DL_DIR/$file" "$url" || continue
    else python3 -c 'import sys,urllib.request;urllib.request.urlretrieve(sys.argv[1],sys.argv[2])' "$url" "$DL_DIR/$file" || continue; fi
    if echo "$sha  $DL_DIR/$file" | sha256sum -c --status; then echo "ok  $file"; return; fi
    echo "SHA-256 mismatch for $file from $url" >&2; rm -f "$DL_DIR/$file"
  done
  echo "could not obtain $file" >&2; exit 1
}

[ -n "${TOOLCHAIN_DIR:-}" ] || download "$TOOLCHAIN_SHA256" "${TOOLCHAIN_NAME}.tar.xz" "$TOOLCHAIN_URL"
download "$ZLIB_SHA256"      "zlib-${ZLIB_VER}.tar.gz"   "$ZLIB_URL" "$ZLIB_URL2"
download "$OPENSSL_SHA256"   "openssl-${OPENSSL_VER}.tar.gz" "$OPENSSL_URL"
download "$CURL_SHA256"      "curl-${CURL_VER}.tar.xz"   "$CURL_URL"
echo "all inputs present in $DL_DIR"
