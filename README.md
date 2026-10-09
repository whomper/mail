# MAIL

<img src="docs/icon16.png" width="96" align="right" alt="MAIL icon: an Atari SM124-style monitor showing an envelope">

An e-mail program for the Atari ST, STE, TT and Falcon, written in C for
GEM. It reads mail over IMAP or POP3, sends it over SMTP, keeps a mirror
of your IMAP folders on disk so you can read offline, and reads and
writes Hebrew.

- IMAP with a local mirror: headers of the newest messages and every
  message you've read, kept in step with the server; big folders load
  100 messages at a time, with "Load more" for older ones
- POP3 that downloads into local folders and never fetches a message twice
- SMTP with login (AUTH PLAIN/LOGIN), an Outbox for sending later, copies
  in Sent
- Reply, reply to all, forward (with the original's attachments), flags,
  move, delete to Trash, folders, address book, signatures
- Attachments: save them with the file selector; attach files when writing
- HTML-only mail shown as text
- Hebrew: UTF-8, ISO-8859-8 and windows-1255 mail, Hebrew subjects, names,
  file names and folder names, right-to-left layout, and the Israeli
  keyboard on F10
- One main window with draggable panes (folders, list, message), right-click
  menus, a choice of fonts (GDOS fonts too), passwords hidden behind an eye
  button; window and pane sizes are kept between sessions
- Runs on TOS 1.04 to 4.x, EmuTOS, MagiC and MiNT, with STinG or
  MiNTnet; from a 68000 ST in medium resolution to a Falcon in 640×480

Mail providers want encrypted connections. MAIL reaches them one of two
ways, switched with Options > Falcon mode:

```
off (default):  Atari (MAIL.PRG) --STinG/MiNTnet--> Raspberry Pi (stunnel) --TLS--> provider
on:             Falcon (MAIL.PRG, TLS 1.2, RSA on the DSP) --STinG/MiNTnet--TLS--> provider
```

- **Off**, on any Atari: a Raspberry Pi on your network does the
  encryption, see [gateway/README.md](gateway/README.md). A server that
  accepts plain connections can be used directly.
- **On**, on a Falcon or TT with 4 MB or more: MAIL does TLS 1.2 itself
  with [BearSSL](https://bearssl.org/) (third_party/bearssl), built for
  the 68030, and the Falcon's DSP56001 checks the servers' RSA
  signatures. It needs `CACERT.PEM` and `ROOTS.DAT` (in this repository)
  next to MAIL.PRG. See [Falcon mode](docs/GUIDE.md#falcon-mode).

MAIL follows the design of Troll, the GFA-BASIC newsreader and mail
client by Rajah Lone: the same four windows (folders, message list,
message, editor), IMAP mirroring, and STinG or MiNTnet networking. It
reuses parts of Claude ST
([whomper/atari_claude](https://github.com/whomper/atari_claude)): the
freestanding TOS runtime, GEM bindings, STinG client, Hebrew
right-to-left layout and keyboard, and the Hatari test tools.

## Getting started

1. Copy `MAIL.PRG` into a folder of its own on your Atari, e.g. `C:\MAIL\`.
2. Set up the gateway on a Raspberry Pi: `sudo gateway/install.sh
   imap.gmail.com smtp.gmail.com` (see [gateway/README.md](gateway/README.md)).
3. Start MAIL and fill in your account: the Pi's IP address, ports 143
   (IMAP) and 587 (SMTP), your login.

Or, on a Falcon or TT with 4 MB: copy `MAIL.PRG`, `CACERT.PEM` and
`ROOTS.DAT` into the folder, skip the Pi, and switch on Options > Falcon
mode; MAIL fills in the servers for the big providers.

The [user guide](docs/GUIDE.md) describes everything else.

## Building

```
sudo apt install gcc-m68k-linux-gnu      # Debian/Ubuntu
make                                     # writes MAIL.PRG and ROOTS.DAT
```

No MiNTLib is needed: the program is freestanding (`atari/`) and
`tools/elf2tos.py` writes the TOS executable. It is built for the 68000,
so it runs on every Atari from the ST to the Falcon; only BearSSL, used in
Falcon mode, is 68030 code. ROOTS.DAT is CACERT.PEM decoded on the build
computer by `tools/mkroots.c`, so the Atari doesn't have to.
The DSP program is `dsp/rsa.a56`; `dsp/mkh.py` assembles it into
`dsp/rsa_prog.h` (with the `a56` assembler: `cd dsp && a56 rsa.a56 && python3 mkh.py a56.out rsa_prog.h dsp_rsa_prog`).

## Source

```
atari/   TOS runtime: start-up, system calls, a small C library, AES/VDI,
         TCP over STinG/STiK or MiNTnet (with a DNS client)
src/     the mail core: IMAP, POP3, SMTP, MIME, charsets and Hebrew,
         bidi, the local store, composing, mail operations, TLS (tls.c)
dsp/     the DSP56001 program for RSA (Falcon mode)
third_party/bearssl/  BearSSL 0.6, unchanged
ui/      the GEM program: windows, drawing, editor, dialogs, menus
gateway/ the Raspberry Pi gateway (stunnel)
tests/   unit and integration tests, the Hatari test rig
tools/   elf2tos.py, the icon generator, FAKESTNG.PRG (for testing in Hatari only)
icons/   MAIL's desktop icon (RSC and ICN files)
```

## Tests

The mail core (`src/`) also builds on Linux, where it is tested against
real servers before it runs on the Atari:

```
make test           # charsets (Hebrew), MIME, message building, bidi
make itest          # IMAP, POP3, SMTP against a local Dovecot and a test SMTP server
TLS=1 make itest    # the same through the gateway's stunnel set-up, TLS-only servers
FALCON=1 make itest # Falcon mode: MAIL's own TLS, implicit TLS and STARTTLS
```

`make itest` needs `dovecot-imapd` and `dovecot-pop3d`; `TLS=1` also
needs `stunnel4`.

`tests/hatari/` runs MAIL.PRG itself in the
[Hatari](https://hatari.tuxfamily.org/) emulator with
[EmuTOS](https://emutos.sourceforge.io/), with Xvfb and xdotool:

```
make -C tools/fakesting
tests/hatari/net_test.py /tmp/mail-net etos512us.img   # Falcon, real servers
tests/hatari/ui_tour.py WORKDIR etos512us.img          # screenshots of the interface
tests/hatari/falcon_test.py /tmp/mail-tls etos512us.img --dsp emu   # Falcon mode, TLS + DSP
```

`net_test.py` boots an emulated Falcon, where MAIL logs in to Dovecot,
mirrors the inbox, opens a Hebrew message and sends a Hebrew reply; the
test checks the reply and its copy in Sent on the server. Hatari has no
network card, so `FAKESTNG.PRG` stands in for STinG and carries MAIL's
connections over the emulated serial port to `serial_bridge.py`. It's
for the emulator only; never install it on a real Atari.
