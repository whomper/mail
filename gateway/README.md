# The Raspberry Pi mail gateway

Mail providers only accept encrypted connections (TLS) today, and a 16 MHz
Falcon can't do modern TLS at a usable speed. So MAIL talks plain IMAP,
POP3 and SMTP to a Raspberry Pi on your home network, and the Pi talks TLS
to your provider. The Pi runs [stunnel](https://www.stunnel.org/), set up
by `install.sh`.

```
Atari (MAIL.PRG) --plain, home LAN--> Raspberry Pi (stunnel) --TLS--> imap.gmail.com:993
                                                              --TLS--> smtp.gmail.com:465
```

Any computer with Debian, Ubuntu or Raspberry Pi OS works as the gateway,
and the same Pi can also run Claude ST's bridge.

## Setting it up

On the Pi:

```
git clone https://github.com/whomper/MAIL
cd MAIL/gateway
sudo ./install.sh imap.gmail.com smtp.gmail.com pop.gmail.com
```

Name your provider's servers: the IMAP server, the SMTP server and,
if you want POP3, the POP3 server. The script installs stunnel, starts it
now and at every boot, and prints what to type into MAIL. Run it again to
change servers.

| Provider    | IMAP                  | SMTP                  | POP3                 |
|-------------|-----------------------|-----------------------|----------------------|
| Gmail       | imap.gmail.com        | smtp.gmail.com        | pop.gmail.com        |
| iCloud      | imap.mail.me.com      | smtp.mail.me.com *    |                      |
| Fastmail    | imap.fastmail.com     | smtp.fastmail.com     | pop.fastmail.com     |
| Yahoo       | imap.mail.yahoo.com   | smtp.mail.yahoo.com   | pop.mail.yahoo.com   |
| GMX         | imap.gmx.net          | mail.gmx.net          | pop.gmx.net          |

\* iCloud only offers SMTP with STARTTLS on port 587; see below.

Gmail, iCloud and Yahoo need an **app password** for programs like MAIL:
create one in your account's security settings and use it as MAIL's
password. Outlook.com and Microsoft 365 accept only OAuth sign-in, which
MAIL can't do.

## In MAIL

Options > Accounts, with the Pi's address (the script prints it):

| Field                 | Value                          |
|-----------------------|--------------------------------|
| Incoming mail         | IMAP (or POP3)                 |
| Server                | the Pi, e.g. 192.168.1.10      |
| Port                  | 1143 (POP3: 1110)              |
| User, Password        | your mail login / app password |
| Outgoing server       | the Pi again                   |
| Outgoing port         | 1025                           |
| Outgoing user         | empty (the same login)         |

Use the Pi's IP address: plain TOS with STinG can't always resolve names
on the local network.

## Next to another bridge

MAIL converts mail to the Atari character set itself, Hebrew included, so
it only needs a plain TLS tunnel. A bridge made for older programs such
as Troll, which converts the text on the Pi, also works: MAIL notices
Hebrew that is already in Atari characters. If that bridge already uses
the usual ports, give MAIL's gateway others:

```
sudo ./install.sh --ports 2143,2025,2110 imap.gmail.com smtp.gmail.com
```

and type those ports into MAIL's account dialog.

## Keeping it private

Between the Atari and the Pi the connection is not encrypted, as on
every network in the 1990s: keep the gateway on your home network, never
forward its ports on your router. If others share your network,
`--allow` limits the gateway to your Atari's address (with ufw):

```
sudo ./install.sh --allow 192.168.1.50 imap.gmail.com smtp.gmail.com
```

## Servers with STARTTLS only

`install.sh` uses the TLS ports (993, 995, 465). For an SMTP server that
offers only port 587 with STARTTLS, edit `/etc/stunnel/atari-mail.conf`:

```
[smtp]
client = yes
accept = 0.0.0.0:1025
connect = smtp.mail.me.com:587
protocol = smtp
checkHost = smtp.mail.me.com
```

and `sudo systemctl restart stunnel4`.

## Tested

`TLS=1 make itest` runs MAIL's whole integration test through this
stunnel configuration, against servers that only speak TLS.
