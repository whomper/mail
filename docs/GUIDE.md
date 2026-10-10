# EMail user guide

EMail is an e-mail program for the Atari ST, STE, Mega ST/STE, TT and
Falcon. It reads mail from IMAP or POP3 servers, sends it over SMTP,
keeps a copy of your IMAP folders on disk so you can read offline, and
reads and writes Hebrew.

## What you need

- An Atari with TOS 1.04 or newer (TOS 4 on the Falcon), EmuTOS, MagiC or
  MiNT, and a screen of at least 640×200 (ST medium); 640×400 (ST high)
  or the Falcon's 640×480 show three windows side by side
- About 400 KB of free memory, more for big mailboxes
- A TCP/IP stack:
  - **STinG** on plain TOS (or STiK), or
  - **MiNTnet** under FreeMiNT (with or without GlueSTiK), or MagiC-Net
- For providers that need encryption (nearly all), one of:
  - a Raspberry Pi or other Linux computer on your network as a gateway,
    see [gateway/README.md](../gateway/README.md) (any Atari), or
  - **Falcon mode**: EMail does the encryption itself (a Falcon, or a TT,
    with 4 MB of memory or more), see [Falcon mode](#falcon-mode)

## Installing

Copy `EMAIL.PRG` into a folder of its own, for example `C:\EMAIL\`. EMail
keeps everything next to itself:

```
C:\EMAIL\EMAIL.PRG      the program
C:\EMAIL\EMAIL.INF      settings and accounts (each setting has a note above it)
C:\EMAIL\EMAIL.KEY      the key the passwords in EMAIL.INF are encrypted with
C:\EMAIL\ADDRESS.TXT   address book
C:\EMAIL\EMAIL.LOG      protocol log, when switched on
C:\EMAIL\CACERT.PEM    root certificates, for Falcon mode only
C:\EMAIL\ROOTS.DAT     the same, decoded ahead of time (Falcon mode only)
C:\EMAIL\EMAIL\         your mail: one folder per account, one per mailbox
```

Start it. The first time, EMail asks for your account.

## Setting up an account

Options > Accounts > New, or the dialog EMail shows at first start:

| Field          | What to type                                                     |
|----------------|------------------------------------------------------------------|
| Account name   | a name for the folder list, e.g. "Home"                          |
| Your name      | shown to people you write to (Hebrew is fine)                    |
| E-mail         | your address                                                     |
| Incoming mail  | IMAP (folders stay on the server) or POP3 (mail comes to the Atari) |
| Server, Port   | through the gateway: the Pi's IP and 143 (IMAP) or 110 (POP3), as for a server without TLS |
| User, Password | your login (for Gmail: an app password)                          |
| POP3: leave mail on the server | keep a copy on the server after downloading    |
| Outgoing       | the Pi's IP and 587 through the gateway; leave User empty to use the incoming login, or type `-` for a server that needs none |
| Passwords      | shown as `*`; click the eye beside the field to see what you typed |
| Signature      | two lines, added below new messages (a `|` in line 2 starts a third) |

EMail holds up to eight accounts. Edit or delete one with Options >
Accounts > Edit. Passwords are kept in `EMAIL.INF` encrypted with a random
key EMail makes the first time, in `EMAIL.KEY`: someone who reads or copies
EMAIL.INF alone can't read them, but whoever has both files can, so keep
EMAIL.KEY to yourself. Lose EMAIL.KEY (or start with a new one) and EMail asks
you to type the passwords again.

## Falcon mode

EMail works in one of two ways, chosen with **Options > Falcon mode
(TLS)** or the checkbox in Options > Settings:

- **Off** (the default, any Atari): EMail talks plainly to the Raspberry
  Pi gateway, and the Pi encrypts the connection to your provider.
- **On**: EMail encrypts on the Atari itself (TLS 1.2) and talks straight
  to your provider; no Pi is needed. On a Falcon the DSP56001 checks the
  servers' certificates (about 0.2 s instead of 1.3 s on the 68030). "use the
  DSP" in Options > Settings switches that off, leaving all of it to the
  68030 (slower, but a way to rule the DSP out if something goes wrong).

Falcon mode needs:

- a 68030 (Falcon, or TT without the DSP part); a 68000 ST is too slow
- 4 MB of memory or more (1 MB is not enough for TLS next to your mail)
- `CACERT.PEM` and `ROOTS.DAT` next to `EMAIL.PRG`, both from EMail's
  GitHub page. CACERT.PEM holds the root certificates EMail trusts (the
  Mozilla list); ROOTS.DAT is the same list already decoded, which saves
  over a minute on the first connection. If you replace CACERT.PEM with a
  newer one (from [curl.se/docs/caextract.html](https://curl.se/docs/caextract.html)),
  EMail decodes it once on the next connection and writes a new ROOTS.DAT
- the right date, time and time zone (Options > Settings): certificates
  are only valid between two dates
- EMail in fast (TT) RAM if you have it: EMAIL.PRG asks TOS for it, and
  TLS runs twice as fast there as in ST RAM

Each account keeps two sets of servers and logins, so you can switch back
and forth without typing them again: the gateway's (used when Falcon mode
is off) and the provider's own (used when it is on). Options > Accounts >
Edit shows the set of the mode you are in, and changing a password in one
mode leaves the other alone. The first time, Falcon mode's login starts as
a copy of the gateway's. Your name, address and signature are shared. When you switch
it on, EMail fills them in for Gmail, iCloud, Yahoo, GMX, web.de, AOL,
Fastmail and Zoho from the e-mail address, and names any account whose
servers you still have to type in. The usual ports:

| Port | What it is                                       |
|------|--------------------------------------------------|
| 993  | IMAP over TLS                                    |
| 995  | POP3 over TLS                                    |
| 465  | SMTP over TLS                                    |
| 587  | SMTP, switched to TLS with STARTTLS              |
| 143, 110 | IMAP, POP3, switched to TLS with STARTTLS    |

For iCloud that is `imap.mail.me.com` 993 and `smtp.mail.me.com` 587; for
Gmail `imap.gmail.com` 993 and `smtp.gmail.com` 465. Either way EMail
never sends a password over an unencrypted connection in Falcon mode.

Connecting takes a few seconds on a 50 MHz Falcon; after that mail comes
in at modem-to-ISDN speeds (about 60 KB/s), which is plenty for text.
About > shows how the current connection is made ("TLS with the DSP",
"TLS on the 68030" or "through the gateway"), and the protocol log
records the cipher and the timings.

## The main window

EMail's main window has three panes, like the GFA Troll it follows and
like a modern mail program:

- **Folders** (left): each account with its folders and unread counts.
  Click a folder to open it. Click an account's name to check its mail.
- **Messages** (top right): newest first. Unread messages are bold;
  `!` in the first place is flagged, `@` in the second probably has
  attachments. In
  Sent and the Outbox it shows who you wrote to. Its header shows how
  many messages are loaded and how many there are.
- **Message** (bottom right): the selected message. Click an attachment
  line (`»`) to save it with the file selector. HTML-only mail is shown
  as text. Quoted lines are drawn light.

**Drag the dividers** between the panes to size them; the pointer turns
into a hand over a divider. Drag the box in the bottom right corner to
size the window. Each pane has its own scroll bar. The pane
with the dark header has the keyboard: click a pane, or press Tab to
move on. The main window's own place and size, the dividers, the
editor's place and the font are all kept in `EMAIL.INF` for next time.

Click a message to read it. Up and Down move through the list; Return
moves the keyboard to the message. In the message pane Space and
Backspace page down and up. Closing the main window quits EMail.

**Big folders.** EMail loads a folder 100 messages at a time, newest
first (Options > Settings changes how many). When there are older ones,
the last line of the list says **Load 100 more**: click it, or move onto
it with Down and press Return. Only what you load is mirrored, so a Sent
folder with thousands of messages opens as quickly as a small one.

**Right-click** anything for what you can do with it:

| On            | Menu                                                                   |
|---------------|------------------------------------------------------------------------|
| a message     | Open, Reply, Reply to all, Forward, Mark as read/unread, Flag, Move to, Delete |
| a folder      | Open, Check for new mail, Mark all as read, New folder, Delete folder  |
| an account    | Check mail, New message, Edit account, Refresh folder list             |
| an attachment | Save attachment                                                        |
| the editor    | Send now, Put in Outbox, Attach file, Address book, Hebrew keyboard    |

Pick with a click, press-drag-release, or the arrow keys and Return;
Esc closes the menu.

**New message** opens the editor in a window of its own.

**Options > Font** opens a font selector like other GEM programs': the
font families on the left, the sizes of the chosen one beside them,
what kind of font it is (bitmap, Speedo, TrueType, Type 1) and a sample
below. The system font comes in two sizes (8×8 and 8×16); with a GDOS
such as NVDI or SpeedoGDOS its fonts are listed too. EMail lays text out
in character cells, so it needs a monospaced font: proportional ones
are shown light and can't be chosen. Menus and dialogs keep the system
font.

**Hebrew letters.** Fonts keep the Hebrew letters in different places:
the standard Atari font (TOS, EmuTOS) at the Atari character set's,
Israeli system fonts and many GDOS fonts where ISO-8859-8 or DOS 862 put
them. If Hebrew shows as other symbols, open Options > Font: the three
buttons under "Hebrew letters" each show the word shalom as that kind of
font would; pick the one that reads correctly, and the sample below
shows the result.

**About EMail** (first menu) shows the version and how EMail is connected.

## Writing

The editor holds the header lines on top and your text below the line:

```
To: Dana <dana@example.com>, avi@example.org
Cc:
Subject: Shalom
Attach: C:\PICS\FALCON.JPG
───────────────────────────
your text...
```

- Several addresses are separated by commas. Add a `Bcc:` line for
  hidden copies.
- Tab jumps to the next header line and, from the last one, into the text.
- Ctrl+B (Message > Address book) adds an address you wrote to before.
- Ctrl+T (Message > Attach file) adds an `Attach:` line; you can also
  type one.
- Lines wrap at 72 characters as you type (Options > Settings).
- **Ctrl+S sends now.** Message > Put in Outbox keeps it for later; it
  goes out with the next Check mail. Closing the window asks whether to
  keep the message in the Outbox.

Replies quote the original and keep the thread together (In-Reply-To and
References). Forwarding a message with attachments attaches the original.

## Hebrew

EMail shows Hebrew mail in UTF-8, ISO-8859-8 or windows-1255, Hebrew
subjects and names (RFC 2047), Hebrew attachment names and Hebrew folder
names. Hebrew paragraphs are laid out right to left and aligned to the
right; numbers and Latin words inside them stay left to right.

Press **F10** (or Options > Hebrew keyboard) to type Hebrew with the
Israeli SI-1452 layout: the keys where the Hebrew letters are on an
Israeli keyboard. Press F10 again for Latin letters. In a Hebrew line the
Left arrow moves forward, as on Hebrew systems. Messages go out in UTF-8,
which every modern program reads.

Mail that comes through a bridge made for older programs such as Troll
(which turns Hebrew into Atari characters on the Pi, and each line
around, because Troll can't lay Hebrew out itself) is recognised and put
back into reading order. If such Hebrew shows with its words in the
wrong order, switch "Bridge sends Hebrew reversed" in Options > Settings.

The Atari font has the 27 Hebrew letters but no vowel points (niqqud),
so those are left out when showing a message.

## Keys

| Key          | Does                       | Key          | Does                    |
|--------------|----------------------------|--------------|-------------------------|
| Ctrl+N       | new message                | Ctrl+K       | check mail              |
| Ctrl+R       | reply                      | Ctrl+E       | reply to all            |
| Ctrl+F       | forward                    | Ctrl+U       | mark as unread          |
| Ctrl+G       | flag / unflag              | Ctrl+M       | move to a folder        |
| Delete       | delete (to Trash)          | Ctrl+S       | send (in the editor)    |
| Ctrl+T       | attach a file              | Ctrl+B       | address book            |
| F10          | Hebrew keyboard on/off     | Help         | these keys              |
| Esc          | close the editor           | Ctrl+Q       | quit                    |
| Tab          | next pane                  | Right button | what you can do there   |
| Ctrl+A       | select all messages        | Shift+click  | add/remove one message  |
| Control+click| select a range             |              |                         |

**Several messages at once.** Shift+click adds a message to the selection
(or takes it out), Control+click selects everything from the last message
you clicked, and Ctrl+A (Message > Select all) the whole list. The message
pane then says how many are selected; Mark as read, Mark as unread, Flag,
Move to and Delete (from the Message menu, the keys or a right-click on the
selection) work on all of them. A plain click selects one message again.

**Move to** shows the folders in alphabetical order, a subfolder with its
parent ("Archive / 2023"); the arrows page through a long list, a click
picks a folder and OK (or a double click) moves the messages there.

## How your mail is kept

**IMAP** accounts are mirrored: EMail keeps the headers of the newest
messages of each folder, as many as you have loaded (100 to start with),
and the messages you have read; the counts in the folder pane are the
server's. Changes made elsewhere (read, deleted, moved on your phone) show up
at the next check. Opening, flagging or marking a message changes it at once
on the Atari; EMail tells the server in one go when you pause for a few
seconds, and at the latest when you quit. A folder you opened in the last two
minutes opens straight from disk; Check mail always asks the server. Deleting moves a message to Trash on the server;
deleting in Trash removes it for good.

**POP3** accounts download new mail into local folders (Inbox, Sent,
Trash) and remember what they fetched, so nothing comes twice. With
"leave mail on the server" off, EMail deletes it from the server once it
is safely on disk.

**Offline** (Options > Work offline): EMail doesn't connect, and you can
read everything already on disk. New messages wait in the Outbox.

**Not connected**: when a connection or login fails, EMail says so once
and then works from the cache for that account, as if offline: messages
already on disk open at once, and one that was never downloaded shows a
note instead of a new attempt. Marking messages read, unread or flagged
works as usual and reaches the server at the next connection. Check mail
(^K, or a click on the account's name) or the timed check connects again.

**Settings** (Options > Settings): your time zone in minutes east of UTC
(Israel: 120 in winter, 180 in summer), automatic checking every N
minutes, how many messages to load at a time, whether read messages stay on disk
after quitting, the protocol log, and the keyboard EMail starts with.

## Folders

Folder > New folder makes a folder on the IMAP server (Hebrew names
work). Folder > Delete folder removes the open one with all its messages;
right-click a folder to do this for any folder, or to mark all its
messages as read.
Folder > Refresh folder list reads the list again after you changed it
elsewhere.

## The desktop icon

`icons/` has EMail's icon for the desktop: `EMAIL.RSC` holds a black and
white and a 16-colour version to copy into `DESKICON.RSC` /
`DESKCICN.RSC` with a resource editor, and `EMAIL.ICN` / `EMAILMK.ICN` are
the image and mask for icon editors. For EmuTOS,
`tools/icon/add_to_emuicon.py` adds it to `EMUICON.RSC`.

## When something goes wrong

- **"can't connect"**: is the gateway running (`sudo systemctl status
  stunnel4` on the Pi)? Is the address right? Is STinG (or MiNTnet) set
  up — can other network programs reach the Pi?
- **Falcon mode: "certificate not trusted"**: is CACERT.PEM next to
  EMAIL.PRG, and is the server name right (the certificate must be for
  that name)? **"certificate out of date"**: check the date, time and
  time zone. **"not enough memory"**: close other programs, put EMail in
  TT RAM, or use the gateway.
- **"login failed"**: check user and password; Gmail, iCloud and Yahoo
  need an app password.
- **Options > Protocol log** writes the conversation with the servers to
  `EMAIL.LOG` (passwords are hidden); it shows exactly what a server said.

## Limits

- Encryption on the Atari itself (Falcon mode) needs a 68030 and 4 MB;
  other machines use the gateway. TLS 1.2 only (every provider has it).
- No OAuth sign-in (Outlook.com, Microsoft 365).
- Messages are plain text; HTML mail is shown as text and attachments are
  saved, not shown.
- Up to 8 accounts, 64 folders per account.
