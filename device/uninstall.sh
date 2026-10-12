#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Undo everything: stop the relay, remove the menu bind mount, delete /data/hd and the /tmp copies.
for p in $(pidof hdrelay 2>/dev/null); do kill $p 2>/dev/null; done
while grep -q "scripts/menu.rss" /proc/mounts; do umount /system/rtk_rootfs/usr/local/bin/scripts/menu.rss || break; done
rm -rf /tmp/hd900b
cd / && rm -rf /data/hd
echo "removed. (The firmware itself was never modified.)"
