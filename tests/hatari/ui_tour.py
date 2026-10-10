#!/usr/bin/env python3
"""A tour of EMAIL.PRG in Hatari, saving a screenshot after each step.

    ui_tour.py WORKDIR EMUTOS.IMG

WORKDIR/hd is the emulated drive C: with EMAIL.PRG, EMAIL.INF and EMAIL\\
(for instance the mail folder left by `KEEP=1 make itest`, with
offline=1 in EMAIL.INF). Screenshots go to WORKDIR/shots.
"""
import sys
import time

import rig
from rig import K, SCAN

work, tos = sys.argv[1], sys.argv[2]
r = rig.Rig(work, tos)
cw, ch = 8, 16

def menu(title_x, item):
    """Open the menu title at pixel x and click item number `item`."""
    r.click(title_x, 8)
    time.sleep(0.4)
    r.click(title_x + 8, 16 + item * ch + 8)
    time.sleep(0.8)

try:
    time.sleep(14)
    r.shot("01-start")

    r.click(330, 124)                    # the second message (in the cache)
    time.sleep(1.5)
    r.shot("02-read-hebrew")

    r.ctrl("n")                          # new message
    time.sleep(1.5)
    r.type("dana@example.com")
    r.key(K["TAB"])
    r.key(K["TAB"])                      # Cc -> Subject
    r.key(K["F10"])                      # Hebrew keyboard
    for k in "akuo":                     # shin lamed vav mem: shalom
        r.key(SCAN[k])
    r.key(K["F10"])
    r.type(" from the Falcon")
    r.key(K["TAB"])                      # into the text
    r.key(K["F10"])
    for k in "akuo":
        r.key(SCAN[k])
    r.key(K["F10"])
    r.type(" Dana, EMail works on the Falcon 030!")
    time.sleep(0.8)
    r.shot("03-compose")

    r.key(K["ESC"])                      # close: keep it?
    time.sleep(1)
    r.shot("04-keep-alert")
    r.key(K["RETURN"])                   # Outbox (the default)
    time.sleep(1.2)
    r.shot("05-queued-alert")
    r.key(K["RETURN"])
    time.sleep(1.2)
    r.shot("06-after-queue")

    menu(256, 1)                         # Options > Settings...
    r.shot("07-settings")
    r.key(K["RETURN"])                   # OK
    time.sleep(1)

    menu(256, 0)                         # Options > Accounts...
    r.shot("08-accounts-alert")
    r.key(K["RETURN"])                   # Edit
    time.sleep(1)
    r.shot("09-pick-account")
finally:
    r.stop()
