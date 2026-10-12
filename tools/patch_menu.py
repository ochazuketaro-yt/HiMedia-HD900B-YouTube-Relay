#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""patch_menu.py -- derive the two RSS files used by the relay from the player's OWN stock firmware files.

The stock files are copyrighted firmware components and are therefore never shipped in this repository:
install.py reads them from your player and runs this patcher locally.

  menu.rss          + one new Premium Apps tile "HD900B_YT" (YouTube icon) that opens hd_ui.rss
  youtube_menu.rss  the stock YouTube screen, its <submenu> list replaced by HOME and SEARCH, both served by hdrelay

Both edits are plain text insertions with asserted anchors; if the firmware differs from the tested one the
patcher stops instead of guessing.
"""
import sys

RELAY = "127.0.0.1:8810"
TILE = "HD900B_YT"
UI_PATH = "rss_file:///tmp/hd900b/lab/hd_ui.rss"


def patch_menu(s: str) -> str:
    if TILE in s:
        raise ValueError("menu.rss already contains the %s tile (read the stock file: umount the bind mount first)" % TILE)
    # 1) the link that the tile opens: add a branch before the final 'else { null; }' of the Shoutcast chain
    a = s.index('title == "Shoutcast"')
    k = s.index('\telse\n\t{\n\t\tnull;\n\t}', a)
    s = s[:k] + '\telse if (title == "%s")\n\t{\n\t\t"%s";\n\t}\n' % (TILE, UI_PATH) + s[k:]
    # 2) register the tile after the Shoutcast tile block
    m = s.index('itemTitleArray, "Shoutcast");')
    m = s.index("}\n", m) + 2
    tile = ('\n{\n\titemTitleArray = pushBackStringArray(itemTitleArray, "%s");\n'
            '\tfocusiconArray = pushBackStringArray(focusiconArray, "image/youtube_focus.fsp");\n'
            '\tunfocusiconArray = pushBackStringArray(unfocusiconArray, "image/youtube_unfocus.fsp");\n'
            '\titemSize = Add(itemSize, 1);\n}\n') % TILE
    return s[:m] + tile + s[m:]


def patch_ui(s: str) -> str:
    a = s.index("<submenu>")
    b = s.rindex("</submenu>") + len("</submenu>")
    thumb = '<media:thumbnail url="image/youtube.gif" width="120" height="90" />'
    home = "<submenu>\n<title>HOME</title>\n<link>http://%s/home.rss</link>\n%s\n</submenu>\n" % (RELAY, thumb)
    search = ('<submenu>\n<title>SEARCH</title>\n<onClick>\n  <script>\n    keyword = getInput("Enter keyword");\n'
              '    if (keyword == NULL) { redrawDisplay(); }\n'
              '    else { url = "http://%s/search?q=" + urlEncode(keyword); }\n  </script>\n</onClick>\n%s\n</submenu>\n') % (RELAY, thumb)
    return s[:a] + home + search + s[b:]


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit("usage: patch_menu.py stock_menu.rss stock_youtube_menu.rss out_hd_menu.rss out_hd_ui.rss")
    rd = lambda p: open(p, encoding="utf-8", newline="").read()
    wr = lambda p, t: open(p, "w", encoding="utf-8", newline="").write(t)
    wr(sys.argv[3], patch_menu(rd(sys.argv[1])))
    wr(sys.argv[4], patch_ui(rd(sys.argv[2])))
    print("wrote", sys.argv[3], sys.argv[4])
