#!/usr/bin/env python3
"""A small SMTP submission server for MAIL's integration tests.

It speaks EHLO, AUTH PLAIN/LOGIN, MAIL, RCPT, DATA, RSET, QUIT and
delivers each message into the Dovecot Maildir of a local test user
(so the test can read it back over IMAP), or into DIR/sink otherwise.

    smtp_server.py PORT DIR USER PASSWORD [CERT KEY]

With CERT and KEY it speaks SMTP over TLS from the first byte (port 465
style), to test the Raspberry Pi gateway.
"""
import base64, os, socketserver, ssl, sys, time

PORT, DIR, USER, PASS = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
TLS = None
if len(sys.argv) > 6:
    TLS = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    TLS.load_cert_chain(sys.argv[5], sys.argv[6])


def deliver(rcpt, data):
    local = rcpt.split("@")[0]
    box = os.path.join(DIR, "home", local, "Maildir")
    if not os.path.isdir(box):
        box = os.path.join(DIR, "sink")
    for d in ("tmp", "new", "cur"):
        os.makedirs(os.path.join(box, d), exist_ok=True)
    name = "%d.%d.mail" % (time.time() * 1e6, os.getpid())
    with open(os.path.join(box, "tmp", name), "wb") as f:
        f.write(data)
    os.rename(os.path.join(box, "tmp", name), os.path.join(box, "new", name))


class H(socketserver.StreamRequestHandler):
    def setup(self):
        if TLS:
            self.request = TLS.wrap_socket(self.request, server_side=True)
        super().setup()

    def out(self, s):
        self.wfile.write(s.encode() + b"\r\n")

    def handle(self):
        self.out("220 test.local ESMTP MAIL test server")
        authed, rcpts, sender = False, [], None
        while True:
            line = self.rfile.readline()
            if not line:
                return
            cmd = line.decode("latin-1").rstrip("\r\n")
            up = cmd.upper()
            if up.startswith("EHLO"):
                self.out("250-test.local")
                self.out("250-8BITMIME")
                self.out("250 AUTH PLAIN LOGIN")
            elif up.startswith("HELO"):
                self.out("250 test.local")
            elif up.startswith("AUTH PLAIN"):
                arg = cmd[11:].strip()
                if not arg:
                    self.out("334 ")
                    arg = self.rfile.readline().decode().strip()
                parts = base64.b64decode(arg).split(b"\0")
                authed = parts[1:] == [USER.encode(), PASS.encode()]
                self.out("235 ok" if authed else "535 bad credentials")
            elif up.startswith("AUTH LOGIN"):
                self.out("334 VXNlcm5hbWU6")
                u = base64.b64decode(self.rfile.readline().strip())
                self.out("334 UGFzc3dvcmQ6")
                p = base64.b64decode(self.rfile.readline().strip())
                authed = (u, p) == (USER.encode(), PASS.encode())
                self.out("235 ok" if authed else "535 bad credentials")
            elif up.startswith("MAIL FROM:"):
                if not authed:
                    self.out("530 authentication required")
                    continue
                sender, rcpts = cmd[10:].strip().strip("<>"), []
                self.out("250 ok")
            elif up.startswith("RCPT TO:"):
                rcpts.append(cmd[8:].strip().strip("<>"))
                self.out("250 ok")
            elif up == "DATA":
                self.out("354 go ahead")
                body = []
                while True:
                    l = self.rfile.readline()
                    if l in (b".\r\n", b".\n", b""):
                        break
                    if l.startswith(b".."):
                        l = l[1:]
                    body.append(l)
                data = b"".join(body)
                for r in rcpts:
                    deliver(r, (b"Return-Path: <%s>\r\nX-Envelope-To: %s\r\n" % (sender.encode(), r.encode())) + data)
                self.out("250 queued")
            elif up == "RSET":
                rcpts = []
                self.out("250 ok")
            elif up == "QUIT":
                self.out("221 bye")
                return
            else:
                self.out("502 not implemented")


class S(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


S(("127.0.0.1", PORT), H).serve_forever()
