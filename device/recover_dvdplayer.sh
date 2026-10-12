#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Recover from a frozen / unresponsive DvdPlayer UI (remote and front buttons dead).
# Kills the DvdPlayer process group; RootApp then starts a fresh DvdPlayer (takes ~10-15 s).
# /tmp, /data and the menu bind mount survive, so the relay and the YouTube tile keep working.
#   usage (Telnet on the player):  sh /data/hd/recover_dvdplayer.sh
for p in /proc/[0-9]*; do
  n=$(cut -d' ' -f2 $p/stat 2>/dev/null)
  case "$n" in "(MAIN)") M=$(basename $p);; esac       # the main DvdPlayer process is named "MAIN"
done
echo "MAIN pid: ${M:-not found}"
[ -n "$M" ] && kill -9 $M
sleep 1
kill -9 $(pidof DvdPlayer) 2>/dev/null
sleep 12
echo "new DvdPlayer pids: $(pidof DvdPlayer | cut -d' ' -f1-3)"
