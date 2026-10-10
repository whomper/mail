#!/usr/bin/env python3
"""End-to-end test of EMAIL.PRG's Falcon mode on an emulated Falcon: EMail
speaks TLS to the servers itself (no gateway).

Dovecot (IMAP) and tests/smtp_server.py run on this computer; Hatari has
no network card, so FAKESTNG.PRG in C:\\AUTO tunnels EMail's TCP
connections over the emulated serial port to serial_bridge.py. EMail
reads the inbox, opens a Hebrew message, and sends a Hebrew reply; the
test then checks on the server that the reply arrived.

    falcon_test.py WORKDIR EMUTOS.IMG     (needs Xvfb, xdotool, dovecot, openssl)

Like net_test.py, but the servers only speak TLS (IMAPS, SMTP on 465
style) and EMAIL.INF has falcon=1. Extra Hatari options can follow, e.g.
--dsp emu to check signatures on the DSP.
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
IMAPS, SMTPS = 10993, 10465

work, tos = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
extra = tuple(sys.argv[3:])
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
# the test certificate: the only root EMail trusts; valid for the address
# EMail uses (the emulator's network bridge has no name lookup)
cert, key = os.path.join(srv, "cert.pem"), os.path.join(srv, "key.pem")
# REUSE=1: the certificate and EMail's ROOTS.DAT from the last run, to
# test a start with the roots cached
keep = os.path.join(work, "keep")
reuse = os.environ.get("REUSE") == "1" and os.path.exists(os.path.join(keep, "ROOTS.DAT"))
if reuse:
    shutil.copy(os.path.join(keep, "cert.pem"), cert)
    shutil.copy(os.path.join(keep, "key.pem"), key)
else:
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
                    "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,DNS:127.0.0.1",
                    "-keyout", key, "-out", cert], check=True, capture_output=True)
os.chmod(key, 0o644)
conf = open(os.path.join(ROOT, "tests", "dovecot.conf.in")).read()
conf = conf.replace("ssl = no", "ssl = yes\nssl_cert = <%s\nssl_key = <%s" % (cert, key))
conf = conf.replace("inet_listener imaps {\n    port = 0", "inet_listener imaps {\n    port = %d" % IMAPS)
for k, v in (("@RUN@", run_dir), ("@DIR@", srv), ("@IMAP@", str(IMAP)), ("@POP3@", "10110"),
             ("@UID@", uid.decode()), ("@GID@", gid.decode())):
    conf = conf.replace(k, v)
with open(os.path.join(srv, "dovecot.conf"), "w") as f:
    f.write(conf)
for m in sorted(os.listdir(os.path.join(ROOT, "tests", "mail"))):
    shutil.copy(os.path.join(ROOT, "tests", "mail", m), os.path.join(srv, "home/dana/Maildir/new", m))
subprocess.run(["chown", "-R", "nobody:", os.path.join(srv, "home")], check=True)
subprocess.run(["dovecot", "-c", os.path.join(srv, "dovecot.conf")], check=True)
smtp = subprocess.Popen([sys.executable, os.path.join(ROOT, "tests", "smtp_server.py"), str(SMTPS), srv, "dana", "secret", cert, key])

# drive C:
os.makedirs(os.path.join(hd, "AUTO"))
shutil.copy(os.path.join(ROOT, "EMAIL.PRG"), hd)
# the full Mozilla list EMail ships, plus the test certificate
with open(os.path.join(hd, "CACERT.PEM"), "wb") as f:
    f.write(open(os.path.join(ROOT, "CACERT.PEM"), "rb").read() + open(cert, "rb").read())
shutil.copy(os.path.join(ROOT, "tools", "fakesting", "FAKESTNG.PRG"), os.path.join(hd, "AUTO"))
if reuse:
    shutil.copy(os.path.join(keep, "ROOTS.DAT"), hd)
elif os.environ.get("NOCACHE") != "1":
    # decoded on this computer by tools/mkroots, as EMail ships it
    # (NOCACHE=1: let EMail decode CACERT.PEM itself, over a minute)
    subprocess.run([os.path.join(ROOT, "build", "mkroots"), os.path.join(hd, "CACERT.PEM"),
                    os.path.join(hd, "ROOTS.DAT")], check=True)
with open(os.path.join(hd, "EMAIL.INF"), "w", newline="") as f:
    # tz=0: the emulator's clock is this computer's, which runs on UTC
    f.write("[options]\r\ntz=0\r\nlog=1\r\nfalcon=1\r\n[account]\r\nname=Dana\r\nfullname=Dana Falcon\r\n"
            "email=dana@test.local\r\nin=imap\r\nhost=127.0.0.1\r\nport=%d\r\nuser=dana\r\n"
            "pass=secret\r\nsmtphost=127.0.0.1\r\nsmtpport=%d\r\nsignature=Sent from my Falcon\r\n"
            "dhost=127.0.0.1\r\ndport=%d\r\ndsec=1\r\ndsmtphost=127.0.0.1\r\ndsmtpport=%d\r\ndsmtpsec=1\r\n"
            % (IMAP, SMTP, IMAPS, SMTPS))

fifo_out, fifo_in = os.path.join(work, "st_out"), os.path.join(work, "st_in")
for p in (fifo_out, fifo_in):
    if os.path.exists(p):
        os.remove(p)
    os.mkfifo(p)
bridge = subprocess.Popen([sys.executable, os.path.join(HERE, "serial_bridge.py"), fifo_out, fifo_in],
                          stderr=open(os.path.join(work, "bridge.log"), "w"))
shots = os.path.join(work, "shots")
shutil.rmtree(shots, ignore_errors=True)
# 4 MB: Falcon mode needs room for TLS (Falcons came with 1, 4 or 14 MB)
r = rig.Rig(work, tos, serial=(fifo_out, fifo_in), extra=("--memsize", "4") + extra)


def wait_log(pattern, timeout):
    """wait until EMAIL.LOG has a line containing pattern"""
    t0 = time.time()
    path = os.path.join(hd, "EMAIL.LOG")
    while time.time() - t0 < timeout:
        if os.path.exists(path) and pattern in open(path, "rb").read().decode("latin-1"):
            return True
        time.sleep(2)
    print("timed out waiting for", pattern)
    return False


ok = False
try:
    wait_log("IMAP -- secure", 400)      # boot, the root certificates, TLS
    time.sleep(40)                       # log in, mirror the inbox
    r.shot("n1-inbox")
    r.click(330, 108)                    # newest message: the Hebrew one
    time.sleep(30)                       # downloaded over the serial line
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
    wait_log("SMTP -- closed", 300)      # another handshake, for SMTP
    time.sleep(20)                       # the copy in Sent
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
    log = os.path.join(hd, "EMAIL.LOG")
    if os.path.exists(log):
        for line in open(log, "rb").read().decode("latin-1").splitlines():
            if " -- " in line or " !! " in line:
                print("log:", line)
    bridge.terminate()
    smtp.terminate()
    subprocess.run(["doveadm", "-c", os.path.join(srv, "dovecot.conf"), "stop"])
    shutil.rmtree(run_dir, ignore_errors=True)
    roots = os.path.join(hd, "ROOTS.DAT")
    if os.path.exists(roots):
        os.makedirs(keep, exist_ok=True)
        for f in (cert, key, roots):
            shutil.copy(f, keep)
print("PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
