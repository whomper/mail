# The Raspberry Pi mail gateway

Mail providers only accept encrypted connections (TLS) today, and a 16 MHz
Falcon can't do modern TLS at a usable speed. So EMail talks plain IMAP,
POP3 and SMTP to a Raspberry Pi on your home network, and the Pi talks TLS
to your provider. The Pi runs [stunnel](https://www.stunnel.org/), set up
by `install.sh`.

```
Atari (EMAIL.PRG) --plain, home LAN--> Raspberry Pi (stunnel) --TLS--> imap.gmail.com:993
                                                              --TLS--> smtp.gmail.com:465
```

Any computer with Debian, Ubuntu or Raspberry Pi OS works as the gateway,
and the same Pi can also run Claude ST's bridge.

## Setting it up

On the Pi:

```
git clone https://github.com/whomper/EMail
cd EMail/gateway
sudo ./install.sh imap.gmail.com smtp.gmail.com pop.gmail.com
```

Name your provider's servers: the IMAP server, the SMTP server and,
if you want POP3, the POP3 server. The script installs stunnel, starts it
now and at every boot, and prints what to type into EMail. Run it again to
change servers.

| Provider    | IMAP                  | SMTP                  | POP3                 |
|-------------|-----------------------|-----------------------|----------------------|
| Gmail       | imap.gmail.com        | smtp.gmail.com        | pop.gmail.com        |
| iCloud      | imap.mail.me.com      | smtp.mail.me.com *    |                      |
| Fastmail    | imap.fastmail.com     | smtp.fastmail.com     | pop.fastmail.com     |
| Yahoo       | imap.mail.yahoo.com   | smtp.mail.yahoo.com   | pop.mail.yahoo.com   |
| GMX         | imap.gmx.net          | mail.gmx.net          | pop.gmx.net          |

\* iCloud only offers SMTP with STARTTLS on port 587; see below.

Gmail, iCloud and Yahoo need an **app password** for programs like EMail:
create one in your account's security settings and use it as EMail's
password. Outlook.com and Microsoft 365 accept only OAuth sign-in, which
EMail can't do.

## In EMail

Options > Accounts, with the Pi's address (the script prints it):

| Field                 | Value                          |
|-----------------------|--------------------------------|
| Incoming mail         | IMAP (or POP3)                 |
| Server                | the Pi, e.g. 192.168.1.10      |
| Port                  | 143 (POP3: 110)                |
| User, Password        | your mail login / app password |
| Outgoing server       | the Pi again                   |
| Outgoing port         | 587                            |
| Outgoing user         | empty (the same login)         |

Use the Pi's IP address: plain TOS with STinG can't always resolve names
on the local network.

## Next to the Troll proxy

EMail converts mail to the Atari character set itself, Hebrew included, so
it only needs a plain TLS tunnel: this gateway. Troll has its own proxy
(`falcon_imap_logproxy.py` in
[whomper/atari_web](https://github.com/whomper/atari_web)), which rewrites
messages for Troll; EMail doesn't use it. The two run side by side: this
gateway on the standard ports (143, 587, 110), the Troll proxy on its own
(1143). To give this gateway other ports instead:

```
sudo ./install.sh --ports 1143,1025,1110 imap.mail.me.com smtp.mail.me.com:587
```

## Keeping it private

If the Pi's firewall (ufw) is on, let the Atari in on the gateway's ports,
or it can't connect:

```
sudo ufw allow from ATARI_IP to any port 143 proto tcp
sudo ufw allow from ATARI_IP to any port 587 proto tcp
```

Between the Atari and the Pi the connection is not encrypted, as on
every network in the 1990s: keep the gateway on your home network, never
forward its ports on your router. If others share your network,
`--allow` limits the gateway to your Atari's address (with ufw):

```
sudo ./install.sh --allow 192.168.1.50 imap.gmail.com smtp.gmail.com
```

## Servers with STARTTLS only

`install.sh` uses the TLS ports (993, 995, 465). For an SMTP server that
takes mail only on port 587 with STARTTLS, such as iCloud, add `:587`:

```
sudo ./install.sh imap.mail.me.com smtp.mail.me.com:587
```

## Tested

`TLS=1 make itest` runs EMail's whole integration test through this
stunnel configuration, against servers that only speak TLS.
