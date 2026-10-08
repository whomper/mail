# MAIL user guide

MAIL is an e-mail program for the Atari ST, STE, Mega ST/STE, TT and
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
- For providers that need encryption (nearly all): a Raspberry Pi or
  other Linux computer on your network as a gateway, see
  [gateway/README.md](../gateway/README.md)

## Installing

Copy `MAIL.PRG` into a folder of its own, for example `C:\MAIL\`. MAIL
keeps everything next to itself:

```
C:\MAIL\MAIL.PRG      the program
C:\MAIL\MAIL.INF      settings and accounts
C:\MAIL\ADDRESS.TXT   address book
C:\MAIL\MAIL.LOG      protocol log, when switched on
C:\MAIL\MAIL\         your mail: one folder per account, one per mailbox
```

Start it. The first time, MAIL asks for your account.

## Setting up an account

Options > Accounts > New, or the dialog MAIL shows at first start:

| Field          | What to type                                                     |
|----------------|------------------------------------------------------------------|
| Account name   | a name for the folder list, e.g. "Home"                          |
| Your name      | shown to people you write to (Hebrew is fine)                    |
| E-mail         | your address                                                     |
| Incoming mail  | IMAP (folders stay on the server) or POP3 (mail comes to the Atari) |
| Server, Port   | through the gateway: the Pi's IP, 1143 (IMAP) or 1110 (POP3); a server without TLS directly: its name and 143 or 110 |
| User, Password | your login (for Gmail: an app password)                          |
| POP3: leave mail on the server | keep a copy on the server after downloading    |
| Outgoing       | the Pi's IP and 1025 through the gateway; leave User empty to use the incoming login, or type `-` for a server that needs none |
| Signature      | added below new messages; `|` starts a new line                  |

MAIL holds up to eight accounts. Edit or delete one with Options >
Accounts > Edit. Passwords are kept in `MAIL.INF` as typed, so keep that
file to yourself.

## The windows

MAIL has four windows, like the GFA Troll it follows:

- **Folders** (left): each account with its folders and unread counts.
  Click a folder to open it. Click an account's name to check its mail.
- **Messages** (top right): newest first. Unread messages are bold with
  a dot; `!` is flagged, `R` answered, `@` probably has attachments. In
  Sent and the Outbox it shows who you wrote to.
- **Message** (bottom right): the selected message. Click an attachment
  line (`»`) to save it with the file selector. HTML-only mail is shown
  as text. Quoted lines are drawn light.
- **New message**: the editor, opened by New, Reply and Forward.

Click a message to read it. Up and Down move through the list; Return
brings the message window to the front. In the message window Space and
Backspace page down and up.

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

MAIL shows Hebrew mail in UTF-8, ISO-8859-8 or windows-1255, Hebrew
subjects and names (RFC 2047), Hebrew attachment names and Hebrew folder
names. Hebrew paragraphs are laid out right to left and aligned to the
right; numbers and Latin words inside them stay left to right.

Press **F10** (or Options > Hebrew keyboard) to type Hebrew with the
Israeli SI-1452 layout: the keys where the Hebrew letters are on an
Israeli keyboard. Press F10 again for Latin letters. In a Hebrew line the
Left arrow moves forward, as on Hebrew systems. Messages go out in UTF-8,
which every modern program reads.

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

## How your mail is kept

**IMAP** accounts are mirrored: MAIL keeps the headers of the newest 300
messages of each folder (Options > Settings) and the messages you have
read. Changes made elsewhere (read, deleted, moved on your phone) show up
at the next check. Deleting moves a message to Trash on the server;
deleting in Trash removes it for good.

**POP3** accounts download new mail into local folders (Inbox, Sent,
Trash) and remember what they fetched, so nothing comes twice. With
"leave mail on the server" off, MAIL deletes it from the server once it
is safely on disk.

**Offline** (Options > Work offline): MAIL doesn't connect, and you can
read everything already on disk. New messages wait in the Outbox.

**Settings** (Options > Settings): your time zone in minutes east of UTC
(Israel: 120 in winter, 180 in summer), automatic checking every N
minutes, how many headers to keep, whether read messages stay on disk
after quitting, the protocol log, and the keyboard MAIL starts with.

## Folders

Folder > New folder makes a folder on the IMAP server (Hebrew names
work). Folder > Delete folder removes the open one with all its messages.
Folder > Refresh folder list reads the list again after you changed it
elsewhere.

## When something goes wrong

- **"can't connect"**: is the gateway running (`sudo systemctl status
  stunnel4` on the Pi)? Is the address right? Is STinG (or MiNTnet) set
  up — can other network programs reach the Pi?
- **"login failed"**: check user and password; Gmail, iCloud and Yahoo
  need an app password.
- **Options > Protocol log** writes the conversation with the servers to
  `MAIL.LOG` (passwords are hidden); it shows exactly what a server said.

## Limits

- No encryption on the Atari itself: use the gateway for TLS.
- No OAuth sign-in (Outlook.com, Microsoft 365).
- Messages are plain text; HTML mail is shown as text and attachments are
  saved, not shown.
- Up to 8 accounts, 64 folders per account.
