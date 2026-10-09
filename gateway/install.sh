#!/bin/bash
# Set up a Raspberry Pi (or any Debian/Ubuntu/Raspberry Pi OS computer)
# as MAIL's secure-mail gateway: the Atari talks plain IMAP, POP3 and
# SMTP to the Pi on your home network, and the Pi talks TLS to your mail
# provider, with stunnel.
#
#   sudo ./install.sh imap.gmail.com smtp.gmail.com [pop.gmail.com]
#   sudo ./install.sh --allow 192.168.1.50 imap.gmail.com smtp.gmail.com
#   sudo ./install.sh imap.mail.me.com smtp.mail.me.com:587     (iCloud)
#
# --allow ATARI_IP  only that address may use the gateway (needs ufw).
# --ports I,S,P     the ports on the Pi for IMAP, SMTP and POP3, when the
#                   usual ones are taken (e.g. by another bridge):
#   sudo ./install.sh --ports 1143,1025,1110 imap.gmail.com smtp.gmail.com
#
# Ports on the Pi, to type into MAIL's account dialog (unless --ports):
#   IMAP 143   POP3 110   SMTP 587   (the standard ones)
set -euo pipefail

USAGE="usage: install.sh [--allow ATARI_IP] [--ports IMAP,SMTP,POP3] IMAP_SERVER SMTP_SERVER [POP3_SERVER]"
ALLOW=""
P_IMAP=143 P_SMTP=587 P_POP3=110
while [ "${1:-}" = "--allow" ] || [ "${1:-}" = "--ports" ]; do
  if [ "$1" = "--allow" ]; then
    ALLOW=${2:?$USAGE}
  else
    IFS=, read -r P_IMAP P_SMTP P_POP3 <<<"${2:?$USAGE}"
    P_POP3=${P_POP3:-110}
  fi
  shift 2
done
IMAP=${1:?$USAGE}
SMTP=${2:?$USAGE}
POP3=${3:-}
# SMTP_SERVER:587 for providers that take mail only with STARTTLS
# (iCloud: smtp.mail.me.com:587); otherwise port 465, TLS from the start
SMTP_PORT=465
if [[ "$SMTP" == *:* ]]; then
  SMTP_PORT=${SMTP##*:}
  SMTP=${SMTP%%:*}
fi
SMTP_PROTO=""
[ "$SMTP_PORT" = 587 ] && SMTP_PROTO="protocol = smtp"

if [ "$(id -u)" != 0 ]; then
  echo "Please run with sudo." >&2
  exit 1
fi

apt-get update -qq
apt-get install -y -qq stunnel4

CONF=/etc/stunnel/atari-mail.conf
cat > "$CONF" <<CONF
; MAIL gateway: plain mail protocols from the Atari, TLS to the provider.
; Written by MAIL's gateway/install.sh - run it again to change servers.
; in stunnel's own folder: it writes the file after becoming stunnel4
pid = /run/stunnel4/atari-mail.pid
setuid = stunnel4
setgid = stunnel4
; TLS 1.2 or newer, and check the provider's certificate
sslVersionMin = TLSv1.2
verifyChain = yes
CAfile = /etc/ssl/certs/ca-certificates.crt

[imap]
client = yes
accept = 0.0.0.0:$P_IMAP
connect = $IMAP:993
checkHost = $IMAP

[smtp]
client = yes
accept = 0.0.0.0:$P_SMTP
connect = $SMTP:$SMTP_PORT
$SMTP_PROTO
checkHost = $SMTP
CONF
if [ -n "$POP3" ]; then
  cat >> "$CONF" <<CONF

[pop3]
client = yes
accept = 0.0.0.0:$P_POP3
connect = $POP3:995
checkHost = $POP3
CONF
fi

install -d -o stunnel4 -g stunnel4 -m 755 /run/stunnel4

# other stunnel set-ups on the same ports stop this one from starting
for f in /etc/stunnel/*.conf; do
  [ "$f" = "$CONF" ] && continue
  for p in $P_IMAP $P_SMTP $P_POP3; do
    if grep -Eq "^[[:space:]]*accept[[:space:]]*=[[:space:]]*(.*:)?$p[[:space:]]*$" "$f"; then
      echo "WARNING: $f also uses port $p; move it away (e.g. rename it to .conf.off)." >&2
    fi
  done
done

# Debian's stunnel4 service starts every /etc/stunnel/*.conf
if [ -f /etc/default/stunnel4 ]; then
  sed -i 's/^ENABLED=0/ENABLED=1/' /etc/default/stunnel4
fi
systemctl enable stunnel4 >/dev/null 2>&1 || true
systemctl restart stunnel4

if [ -n "$ALLOW" ]; then
  if command -v ufw >/dev/null; then
    for p in $P_IMAP $P_SMTP $P_POP3; do
      ufw allow from "$ALLOW" to any port $p proto tcp >/dev/null
      ufw deny $p/tcp >/dev/null
    done
    echo "Only $ALLOW may use the gateway (ufw)."
  else
    echo "ufw is not installed: anyone on your network can use the gateway." >&2
  fi
elif command -v ufw >/dev/null && ufw status | grep -q "^Status: active"; then
  # a firewall is on: without a rule the Atari can't reach the gateway
  for p in $P_IMAP $P_SMTP; do
    if ! ufw status | grep -Eq "^$p(/tcp)?[[:space:]]+ALLOW"; then
      echo "WARNING: the firewall (ufw) is on and doesn't let anyone in on port $p." >&2
      echo "         Allow your Atari: sudo ufw allow from ATARI_IP to any port $p proto tcp" >&2
    fi
  done
fi

sleep 2
for p in $P_IMAP $P_SMTP; do
  if ! ss -ltn "( sport = :$p )" | grep -q LISTEN; then
    echo "WARNING: nothing listens on port $p; see: sudo journalctl -u stunnel4 -n 30" >&2
  elif ! ss -ltnp "( sport = :$p )" | grep -q stunnel; then
    echo "WARNING: port $p belongs to another program: $(ss -ltnp "( sport = :$p )" | grep -o 'users:.*')" >&2
  fi
done

IP=$(hostname -I | awk '{print $1}')
cat <<MSG

The gateway is running. In MAIL's account dialog (Options > Accounts):

  Incoming:  IMAP   Server: $IP   Port: $P_IMAP
$( [ -n "$POP3" ] && echo "  (or POP3        Server: $IP   Port: $P_POP3)" )
  Outgoing (SMTP)   Server: $IP   Port: $P_SMTP

with your usual mail user name and password (for Gmail: an app password).
MSG
