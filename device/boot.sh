#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# /data/hd/boot.sh -- (re)start the YouTube relay and re-apply the menu after a power cycle.  Idempotent.
#   usage (Telnet on the player):  sh /data/hd/boot.sh
# Everything it needs lives in /data/hd (persistent flash); it only writes to /tmp (RAM) and adds ONE bind mount.
# Remove everything again with:    sh /data/hd/uninstall.sh
HD=/data/hd
T=/tmp/hd900b
MENU=/system/rtk_rootfs/usr/local/bin/scripts/menu.rss
mkdir -p $T/lab $T/tls $T/relay
cp -f $HD/cacert.pem $T/tls/cacert.pem
cp -f $HD/hd_menu.rss $HD/hd_ui.rss $T/lab/
cp -f $HD/hdrelay $T/relay/hdrelay && chmod +x $T/relay/hdrelay
for p in $(pidof hdrelay 2>/dev/null); do kill $p 2>/dev/null; done
sleep 1
( cd $T/relay && trap '' HUP && ./hdrelay -p 8810 -b 127.0.0.1 -H 480 -m 20 -c $T/tls/cacert.pem -l $T/relay/relay.log </dev/null >$T/relay/stdout.log 2>&1 & )
if ! grep -q "scripts/menu.rss" /proc/mounts; then
  mount --bind $T/lab/hd_menu.rss $MENU
fi
sleep 2
echo "hdrelay pid: $(pidof hdrelay)   menu bind: $(grep -c 'scripts/menu.rss' /proc/mounts)"
echo "log: $T/relay/relay.log"
