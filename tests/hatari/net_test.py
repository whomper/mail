#!/usr/bin/env python3
"""End-to-end test of MAIL.PRG on an emulated Falcon with real servers.

Dovecot (IMAP) and tests/smtp_server.py run on this computer; Hatari has
no network card, so FAKESTNG.PRG in C:\\AUTO tunnels MAIL's TCP
connections over the emulated serial port to serial_bridge.py. MAIL
reads the inbox, opens a Hebrew message, and sends a Hebrew reply; the
test then checks on the server that the reply arrived.

    net_test.py WORKDIR EMUTOS.IMG     (needs Xvfb, xdotool, dovecot)
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
smtp = subprocess.Popen([sys.executable, os.path.join(ROOT, "tests", "smtp_server.py"), str(SMTP), srv, "dana", "secret"])

# drive C:
os.makedirs(os.path.join(hd, "AUTO"))
shutil.copy(os.path.join(ROOT, "MAIL.PRG"), hd)
shutil.copy(os.path.join(ROOT, "tools", "fakesting", "FAKESTNG.PRG"), os.path.join(hd, "AUTO"))
with open(os.path.join(hd, "MAIL.INF"), "w", newline="") as f:
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
    time.sleep(40)                       # boot, log in, mirror the inbox
    r.shot("n1-inbox")
    r.click(330, 88)                     # newest message: the Hebrew one
    time.sleep(12)                       # downloaded over the serial line
    r.shot("n2-read")
    r.ctrl("r")                          # reply
    time.sleep(2)
    r.key(K["F10"])
    for k in "akuo":                     # shalom
        r.key(SCAN[k])
    r.key(K["F10"])
    r.type(" Dana, this reply comes from a Falcon.")
    r.key(K["RETURN"])
    time.sleep(0.5)
    r.shot("n3-reply")
    r.ctrl("s")                          # send now
    time.sleep(35)
    r.shot("n4-sent")

    # did it arrive?
    m = imaplib.IMAP4("127.0.0.1", IMAP)
    m.login("dana", "secret")
    m.select("INBOX")
    typ, data = m.search(None, "ALL")
    subjects = []
    for num in data[0].split():
        typ, msg = m.fetch(num, "(RFC822)")
        e = email.message_from_bytes(msg[0][1])
        subj = str(email.header.make_header(email.header.decode_header(e["Subject"])))
        subjects.append(subj)
        if subj.startswith("Re: ") and "שלום" in subj:
            body = e.get_payload(decode=True).decode(e.get_content_charset() or "utf-8")
            print("REPLY ARRIVED:", subj)
            print(body)
            ok = "שלום Dana, this reply comes from a Falcon." in body and "> " in body
    m.select("Sent")
    typ, data = m.search(None, "ALL")
    print("in Sent:", len(data[0].split()))
    print("inbox subjects:", subjects)
finally:
    r.stop()
    bridge.terminate()
    smtp.terminate()
    subprocess.run(["doveadm", "-c", os.path.join(srv, "dovecot.conf"), "stop"])
    shutil.rmtree(run_dir, ignore_errors=True)
print("PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
