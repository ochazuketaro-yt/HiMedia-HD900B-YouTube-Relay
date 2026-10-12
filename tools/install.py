#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""install.py -- install hd900b-youtube-relay on a HiMedia HD900B over the LAN (Telnet + temporary HTTP server).

    python3 tools/install.py --host 192.168.0.177

What it does (nothing in the firmware flash is touched; everything goes to /data/hd and /tmp):
  1. logs in over Telnet (root, empty password) and checks that this looks like an RTD1186 / Linux 2.6.34 player
  2. starts a temporary HTTP server on this PC (the player's wget downloads from it)
  3. makes the player upload its own stock menu.rss / youtube_menu.rss, patches them here (tools/patch_menu.py)
  4. downloads dist/hdrelay, the CA bundle, the patched RSS files and the helper scripts to /data/hd (SHA-256 checked)
  5. runs /data/hd/boot.sh  -> starts the relay and adds the "YouTube" tile to Premium Apps
Python 3.8+ standard library only.
"""
import argparse
import hashlib
import http.server
import os
import socket
import socketserver
import sys
import tempfile
import threading
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from hdtelnet import Device          # noqa: E402
import patch_menu                    # noqa: E402

SCRIPTS = "/system/rtk_rootfs/usr/local/bin/scripts"
CA_URL = "https://curl.se/ca/cacert.pem"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def local_ip_towards(host):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((host, 9))
        return s.getsockname()[0]
    finally:
        s.close()


def fetch_cacert(dest):
    print("  CA 証明書を %s から取得します" % CA_URL)
    data = urllib.request.urlopen(urllib.request.Request(CA_URL, headers={"User-Agent": "hd900b-youtube-relay-installer"}), timeout=60).read()
    want = urllib.request.urlopen(CA_URL + ".sha256", timeout=60).read().decode().split()[0]
    if hashlib.sha256(data).hexdigest() != want:
        sys.exit("cacert.pem の SHA-256 が一致しません。中止します。")
    with open(dest, "wb") as f:
        f.write(data)


class Handler(http.server.SimpleHTTPRequestHandler):
    serve_dir = "."

    def __init__(self, *a, **k):
        super().__init__(*a, directory=Handler.serve_dir, **k)

    def log_message(self, *a):
        pass

    def do_POST(self):          # the player uploads its stock RSS files here:  wget --post-file=...
        name = os.path.basename(self.path)
        if not name.endswith(".rss"):
            self.send_response(400); self.end_headers(); return
        n = int(self.headers.get("Content-Length", "0"))
        with open(os.path.join(Handler.serve_dir, "upload_" + name), "wb") as f:
            f.write(self.rfile.read(n))
        self.send_response(200); self.send_header("Content-Length", "0"); self.end_headers()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", required=True, help="IP address of the HD900B")
    ap.add_argument("--user", default="root"); ap.add_argument("--password", default="")
    ap.add_argument("--pc-ip", help="this PC's IP as seen from the player (auto-detected)")
    ap.add_argument("--port", type=int, default=8899, help="temporary HTTP server port on this PC (default 8899)")
    ap.add_argument("--cacert", help="use this CA bundle (PEM) instead of downloading curl.se/ca/cacert.pem")
    ap.add_argument("--relay", default=os.path.join(ROOT, "dist", "hdrelay"), help="relay binary (default: dist/hdrelay)")
    ap.add_argument("--yes", action="store_true", help="do not ask for confirmation")
    a = ap.parse_args()

    if not os.path.isfile(a.relay):
        sys.exit("%s がありません。dist/hdrelay を用意するか、build/build.sh でビルドしてください。" % a.relay)
    pc_ip = a.pc_ip or local_ip_towards(a.host)
    print("== HD900B (%s) に接続します / PC は %s:%d" % (a.host, pc_ip, a.port))
    dev = Device(a.host, user=a.user, password=a.password)
    rc, out = dev.run("uname -a; grep -m1 'cpu model' /proc/cpuinfo; df /data")
    print(out)
    if "2.6.34" not in out or "mips" not in out.lower():
        print("!! 想定外のカーネルです(動作確認は HD900B / Linux 2.6.34-VENUS / MIPS 24Kc のみ)。")
        if not a.yes and input("続行しますか? [y/N] ").lower() != "y":
            sys.exit(1)
    elif not a.yes:
        print("このあと /data/hd を作成し、Premium Apps に YouTube タイルを追加します(ファームウェアは書き換えません)。")
        if input("続行しますか? [y/N] ").lower() != "y":
            sys.exit(1)

    tmp = tempfile.mkdtemp(prefix="hdinstall_")
    Handler.serve_dir = tmp
    for name in ("boot.sh", "uninstall.sh", "recover_dvdplayer.sh"):
        with open(os.path.join(ROOT, "device", name), "rb") as f, open(os.path.join(tmp, name), "wb") as g:
            g.write(f.read().replace(b"\r\n", b"\n"))
    with open(a.relay, "rb") as f, open(os.path.join(tmp, "hdrelay"), "wb") as g:
        g.write(f.read())
    if a.cacert:
        with open(a.cacert, "rb") as f, open(os.path.join(tmp, "cacert.pem"), "wb") as g:
            g.write(f.read())
    else:
        fetch_cacert(os.path.join(tmp, "cacert.pem"))

    socketserver.TCPServer.allow_reuse_address = True
    httpd = socketserver.ThreadingTCPServer(("0.0.0.0", a.port), Handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    base = "http://%s:%d" % (pc_ip, a.port)
    try:
        print("== 純正の menu.rss / youtube_menu.rss を端末から取得します")
        dev.run("while grep -q scripts/menu.rss /proc/mounts; do umount %s/menu.rss || break; done" % SCRIPTS)   # re-install: see the stock file
        for n in ("menu.rss", "youtube_menu.rss"):
            rc, out = dev.run("wget -q -O /dev/null --post-file=%s/%s %s/upload/%s" % (SCRIPTS, n, base, n))
            if rc != 0 or not os.path.isfile(os.path.join(tmp, "upload_" + n)):
                sys.exit("%s を取得できませんでした: %s" % (n, out))
        patch_menu.RELAY = "127.0.0.1:8810"
        rd = lambda p: open(p, encoding="utf-8", newline="").read()
        wr = lambda p, t: open(p, "w", encoding="utf-8", newline="").write(t)
        wr(os.path.join(tmp, "hd_menu.rss"), patch_menu.patch_menu(rd(os.path.join(tmp, "upload_menu.rss"))))
        wr(os.path.join(tmp, "hd_ui.rss"), patch_menu.patch_ui(rd(os.path.join(tmp, "upload_youtube_menu.rss"))))
        print("   パッチ適用済み: hd_menu.rss hd_ui.rss")

        print("== /data/hd にファイルを配置します")
        names = ["hdrelay", "cacert.pem", "boot.sh", "uninstall.sh", "recover_dvdplayer.sh", "hd_menu.rss", "hd_ui.rss"]
        dev.run("mkdir -p /data/hd")
        for n in names:
            rc, out = dev.run("wget -q -O /data/hd/%s %s/%s" % (n, base, n), timeout=180)
            if rc != 0:
                sys.exit("%s のダウンロードに失敗: %s" % (n, out))
        dev.run("chmod +x /data/hd/hdrelay /data/hd/boot.sh /data/hd/uninstall.sh /data/hd/recover_dvdplayer.sh")
        rc, out = dev.run("cd /data/hd && sha256sum %s" % " ".join(names))
        have = {l.split()[1]: l.split()[0] for l in out.splitlines() if len(l.split()) == 2}
        for n in names:
            ok = have.get(n) == sha256(os.path.join(tmp, n))
            print("   %-14s %s" % (n, "OK" if ok else "MISMATCH"))
            if not ok:
                sys.exit("ハッシュが一致しません。中止します。")

        print("== boot.sh を実行します")
        rc, out = dev.run("sh /data/hd/boot.sh", timeout=90)
        print(out)
        rc, out = dev.run("wget -q -O - http://127.0.0.1:8810/health")
        print("   health:", out.strip() or "(応答なし)")
        if out.strip() != "ok":
            print("!! 中継が応答しません。/tmp/hd900b/relay/relay.log を確認してください。")
    finally:
        httpd.shutdown()
        dev.close()
    print("\n完了。ホーム画面 → Premium Apps を開き、YouTube アイコンのタイル(HD900B_YT)を選んでください。")
    print("電源を入れ直した後は Telnet で:  sh /data/hd/boot.sh")


if __name__ == "__main__":
    main()
