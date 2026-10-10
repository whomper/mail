#!/usr/bin/env python3
"""An IMAP proxy for the integration tests that behaves like a server
dropping the connection on RENAME: it passes everything through to the
real server and, half a second after a RENAME, closes the connection
(the folder is renamed and the answer has come back by then: the next
command finds the connection gone).

    drop_proxy.py LISTEN_PORT SERVER_PORT
"""
import socket
import sys
import threading

listen, server = int(sys.argv[1]), int(sys.argv[2])


def pump(src, dst, watch):
    try:
        buf = b""
        while True:
            data = src.recv(65536)
            if not data:
                break
            dst.sendall(data)
            if watch:
                buf = (buf + data)[-4096:]
                if b" RENAME " in buf.upper():
                    # let the server do it, then vanish before the answer
                    threading.Event().wait(0.5)
                    break
    except OSError:
        pass
    for s in (src, dst):
        try:
            s.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        s.close()


def serve():
    ls = socket.socket()
    ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(("127.0.0.1", listen))
    ls.listen(8)
    while True:
        c, _ = ls.accept()
        s = socket.create_connection(("127.0.0.1", server))
        threading.Thread(target=pump, args=(c, s, True), daemon=True).start()
        threading.Thread(target=pump, args=(s, c, False), daemon=True).start()


serve()
