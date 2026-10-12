# SPDX-License-Identifier: GPL-3.0-or-later
# Pinned build inputs.  Every download is verified against the SHA-256 below.
# shellcheck shell=bash

TOOLCHAIN_NAME="mips32el--uclibc--stable-2024.05-1"
TOOLCHAIN_URL="https://toolchains.bootlin.com/downloads/releases/toolchains/mips32el/tarballs/${TOOLCHAIN_NAME}.tar.xz"
TOOLCHAIN_SHA256="ae4400a934074a3b1b39fb7ec79fb535da4e65b668f2330ca14576d072f6b13e"

ZLIB_VER="1.3.2"
ZLIB_URL="https://zlib.net/zlib-${ZLIB_VER}.tar.gz"
ZLIB_URL2="https://zlib.net/fossils/zlib-${ZLIB_VER}.tar.gz"
ZLIB_SHA256="bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16"

OPENSSL_VER="3.5.9"
OPENSSL_URL="https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VER}/openssl-${OPENSSL_VER}.tar.gz"
OPENSSL_SHA256="603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a"

CURL_VER="8.22.0"
CURL_URL="https://curl.se/download/curl-${CURL_VER}.tar.xz"
CURL_SHA256="f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7"

# CPU flags of the target (Realtek RTD1186 = MIPS 24Kc, hard-float FPU present, o32 ABI).
HD_CFLAGS="-march=mips32r2 -mtune=24kc -mfp32 -mhard-float -fno-pie -O2"
