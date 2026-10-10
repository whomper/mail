#!/usr/bin/env python3
"""Selecting several messages and moving them, on an emulated Falcon.

Like net_test.py (Dovecot here, FAKESTNG.PRG and serial_bridge.py for the
network), with 16 extra folders so the move dialog pages. EMail selects
two messages with Shift+click, opens Move (^M), pages the folder list
down and back, picks Archive and moves both; the test checks the server.

    select_test.py WORKDIR EMUTOS.IMG     (needs Xvfb, xdotool, dovecot)
"""
import email
import email.header
import imaplib
import os
import shutil
import subprocess
import sys
import time

import rig
from rig import K, SCAN

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
IMAP, SMTP = 10143, 10025

work, tos = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
os.makedirs(work, exist_ok=True)
srv = os.path.join(work, "srv")
hd = os.path.join(work, "hd")
for d in (srv, hd):
    shutil.rmtree(d, ignore_errors=True)
run_dir = subprocess.run(["mktemp", "-d", "/tmp/tdv.XXXXXX"], capture_output=True, text=True).stdout.strip()
for d in ("state", "home/dana/Maildir/new", "home/dana/Maildir/cur", "home/dana/Maildir/tmp"):
    os.makedirs(os.path.join(srv, d))
os.chmod(work, 0o755)
os.chmod(srv, 0o755)
with open(os.path.join(srv, "users"), "w") as f:
    f.write("dana:{PLAIN}secret\n")
uid, gid = subprocess.check_output(["id", "-u", "nobody"]).strip(), subprocess.check_output(["id", "-g", "nobody"]).strip()
conf = open(os.path.join(ROOT, "tests", "dovecot.conf.in")).read()
for k, v in (("@RUN@", run_dir), ("@DIR@", srv), ("@IMAP@", str(IMAP)), ("@POP3@", "10110"),
             ("@UID@", uid.decode()), ("@GID@", gid.decode())):
    conf = conf.replace(k, v)
with open(os.path.join(srv, "dovecot.conf"), "w") as f:
    f.write(conf)
for m in sorted(os.listdir(os.path.join(ROOT, "tests", "mail"))):
    shutil.copy(os.path.join(ROOT, "tests", "mail", m), os.path.join(srv, "home/dana/Maildir/new", m))
subprocess.run(["chown", "-R", "nobody:", os.path.join(srv, "home")], check=True)
subprocess.run(["dovecot", "-c", os.path.join(srv, "dovecot.conf")], check=True)
# enough folders for the move dialog to page, in no particular order
m0 = imaplib.IMAP4("127.0.0.1", IMAP)
m0.login("dana", "secret")
for name in ["Zebra", "Apple", "Receipts", "Travel", "Bills", "Kids", "Music", "Archive", "Archive.2023",
             "Archive.2024", "Work", "Friends", "Old", "News", "Garden", "Car"]:
    m0.create(name)
m0.logout()
smtp = subprocess.Popen([sys.executable, os.path.join(ROOT, "tests", "smtp_server.py"), str(SMTP), srv, "dana", "secret"])

# drive C:
os.makedirs(os.path.join(hd, "AUTO"))
shutil.copy(os.path.join(ROOT, "EMAIL.PRG"), hd)
shutil.copy(os.path.join(ROOT, "tools", "fakesting", "FAKESTNG.PRG"), os.path.join(hd, "AUTO"))
with open(os.path.join(hd, "EMAIL.INF"), "w", newline="") as f:
    f.write("[options]\r\ntz=180\r\nlog=1\r\n[account]\r\nname=Dana\r\nfullname=Dana Falcon\r\n"
            "email=dana@test.local\r\nin=imap\r\nhost=127.0.0.1\r\nport=%d\r\nuser=dana\r\n"
            "pass=secret\r\nsmtphost=127.0.0.1\r\nsmtpport=%d\r\nsignature=Sent from my Falcon\r\n"
            % (IMAP, SMTP))

fifo_out, fifo_in = os.path.join(work, "st_out"), os.path.join(work, "st_in")
for p in (fifo_out, fifo_in):
    if os.path.exists(p):
        os.remove(p)
    os.mkfifo(p)
bridge = subprocess.Popen([sys.executable, os.path.join(HERE, "serial_bridge.py"), fifo_out, fifo_in],
                          stderr=open(os.path.join(work, "bridge.log"), "w"))
shots = os.path.join(work, "shots")
shutil.rmtree(shots, ignore_errors=True)
r = rig.Rig(work, tos, serial=(fifo_out, fifo_in))
ok = False
try:
    time.sleep(45)                       # boot, log in, mirror the inbox
    r.shot("s0-inbox")
    r.click(330, 108)                    # the newest message
    time.sleep(10)
    r.xdo("keydown", "Shift_L")
    r.click(330, 140)                    # Shift+click the third
    r.xdo("keyup", "Shift_L")
    time.sleep(2)
    r.shot("s1-two")
    r.ctrl("m")                          # move them
    time.sleep(3)
    r.shot("s2-picker")
    # the dialog's arrows, its second row (Archive) and OK on a 640x480 screen
    if True:
        dx, dy, rx, ry, ox, oy = 420, 321, 250, 161, 380, 385
        r.click(dx, dy)                  # next page, in place
        time.sleep(2)
        r.shot("s3-page2")
        r.click(dx, dy - 11 * 16)        # back up (the up arrow)
        time.sleep(2)
        r.click(rx, ry)                  # a folder
        time.sleep(1)
        r.shot("s4-chosen")
        r.click(ox, oy)                  # OK
        time.sleep(25)
        r.shot("s5-moved")
        m = imaplib.IMAP4("127.0.0.1", IMAP)
        m.login("dana", "secret")
        count = {}
        for f in ["INBOX", "Archive"]:
            m.select(f)
            count[f] = len(m.search(None, "ALL")[1][0].split())
        print(count)
        ok = count == {"INBOX": 1, "Archive": 2}
finally:
    r.stop()
    bridge.terminate()
    smtp.terminate()
    subprocess.run(["doveadm", "-c", os.path.join(srv, "dovecot.conf"), "stop"])
    shutil.rmtree(run_dir, ignore_errors=True)
print("PASS" if ok else "FAIL")
