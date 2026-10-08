# MAIL

An e-mail client for the Atari ST, STE, TT and Falcon, written in C for
GEM. It reads mail over IMAP or POP3 and sends it over SMTP, keeps a local
mirror of your IMAP folders, and reads and writes Hebrew.

MAIL follows the design of Troll, the
GFA-BASIC newsreader and mail client by Rajah Lone, and reuses parts of
Claude ST ([whomper/atari_claude](https://github.com/whomper/atari_claude)):
its freestanding TOS runtime, GEM bindings, STinG client and Hebrew
right-to-left layout.

> Work in progress: the mail core is complete and tested; the GEM
> interface runs and is being finished. A user guide and the Raspberry Pi
> gateway setup follow.

## Building

```
sudo apt install gcc-m68k-linux-gnu      # Debian/Ubuntu
make                                     # writes MAIL.PRG
```

No MiNTLib is needed: the program is freestanding and `tools/elf2tos.py`
writes the TOS executable.

## Tests

The mail code also builds on Linux, where it is tested against real
servers before it goes to the Atari:

```
make test     # charsets (Hebrew included), MIME, message building, bidi
make itest    # IMAP, POP3 and SMTP against a local Dovecot (needs dovecot-imapd, dovecot-pop3d)
```

`tests/hatari/` drives MAIL.PRG in the Hatari emulator with EmuTOS.
