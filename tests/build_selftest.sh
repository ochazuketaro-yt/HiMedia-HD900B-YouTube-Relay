#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build tests/compat_selftest (static, with the compat layer) -- needs the cross compiler on PATH (see build/build.sh).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-mipsel-linux-gcc}
F="-march=mips32r2 -mtune=24kc -mfp32 -mhard-float -no-pie -fno-pie -O2 -Wall -Wno-nonnull-compare -Wno-missing-attributes"
mkdir -p "$ROOT/tests/out"
$CC $F -c "$ROOT/src/compat/hd900b_compat.c" -o "$ROOT/tests/out/hd900b_compat.o"
$CC $F -static "$ROOT/tests/out/hd900b_compat.o" "$ROOT/tests/compat_selftest.c" -o "$ROOT/tests/out/compat_selftest" -lpthread -Wl,-u,__xstat64_conv
echo "built tests/out/compat_selftest  (copy it to the player and run it; exit code = number of failures)"
