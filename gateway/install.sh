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
#   sudo ./install.sh --ports 2143,2025,2110 imap.gmail.com smtp.gmail.com
#
# Ports on the Pi, to type into MAIL's account dialog (unless --ports):
#   IMAP 1143   POP3 1110   SMTP 1025
set -euo pipefail

USAGE="usage: install.sh [--allow ATARI_IP] [--ports IMAP,SMTP,POP3] IMAP_SERVER SMTP_SERVER [POP3_SERVER]"
ALLOW=""
P_IMAP=1143 P_SMTP=1025 P_POP3=1110
while [ "${1:-}" = "--allow" ] || [ "${1:-}" = "--ports" ]; do
  if [ "$1" = "--allow" ]; then
    ALLOW=${2:?$USAGE}
  else
    IFS=, read -r P_IMAP P_SMTP P_POP3 <<<"${2:?$USAGE}"
    P_POP3=${P_POP3:-1110}
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
pid = /run/stunnel-atari-mail.pid
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
fi

IP=$(hostname -I | awk '{print $1}')
cat <<MSG

The gateway is running. In MAIL's account dialog (Options > Accounts):

  Incoming:  IMAP   Server: $IP   Port: $P_IMAP
$( [ -n "$POP3" ] && echo "  (or POP3        Server: $IP   Port: $P_POP3)" )
  Outgoing (SMTP)   Server: $IP   Port: $P_SMTP

with your usual mail user name and password (for Gmail: an app password).
MSG
