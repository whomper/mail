#!/bin/bash
# Set up a Raspberry Pi (or any Debian/Ubuntu/Raspberry Pi OS computer)
# as MAIL's secure-mail gateway: the Atari talks plain IMAP, POP3 and
# SMTP to the Pi on your home network, and the Pi talks TLS to your mail
# provider, with stunnel.
#
#   sudo ./install.sh imap.gmail.com smtp.gmail.com [pop.gmail.com]
#   sudo ./install.sh --allow 192.168.1.50 imap.gmail.com smtp.gmail.com
#
# --allow ATARI_IP  only that address may use the gateway (needs ufw).
#
# Ports on the Pi, to type into MAIL's account dialog:
#   IMAP 1143   POP3 1110   SMTP 1025
set -euo pipefail

ALLOW=""
if [ "${1:-}" = "--allow" ]; then
  ALLOW=$2
  shift 2
fi
IMAP=${1:?usage: install.sh [--allow ATARI_IP] IMAP_SERVER SMTP_SERVER [POP3_SERVER]}
SMTP=${2:?usage: install.sh [--allow ATARI_IP] IMAP_SERVER SMTP_SERVER [POP3_SERVER]}
POP3=${3:-}

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
accept = 0.0.0.0:1143
connect = $IMAP:993
checkHost = $IMAP

[smtp]
client = yes
accept = 0.0.0.0:1025
connect = $SMTP:465
checkHost = $SMTP
CONF
if [ -n "$POP3" ]; then
  cat >> "$CONF" <<CONF

[pop3]
client = yes
accept = 0.0.0.0:1110
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
    for p in 1143 1025 1110; do
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

  Incoming:  IMAP   Server: $IP   Port: 1143
$( [ -n "$POP3" ] && echo "  (or POP3        Server: $IP   Port: 1110)" )
  Outgoing (SMTP)   Server: $IP   Port: 1025

with your usual mail user name and password (for Gmail: an app password).
MSG
