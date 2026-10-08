#!/bin/bash
# Integration test of MAIL's mail core against a real IMAP/POP3 server
# (Dovecot) and an SMTP server, all on localhost. Needs dovecot-imapd and
# dovecot-pop3d installed; run with `make itest`.
#
# TLS=1 runs the same tests through the Raspberry Pi gateway set-up: the
# servers only speak TLS (IMAPS, POP3S, SMTP on 465 style) and stunnel,
# configured like gateway/install.sh does, offers the plain ports MAIL
# uses. Needs stunnel4 and openssl.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CLI=$ROOT/build/mail-cli
D=${MAIL_TEST_DIR:-$(mktemp -d /tmp/mail-itest.XXXXXX)}
IMAP=${IMAP_PORT:-10143}; POP3=${POP3_PORT:-10110}; SMTP=${SMTP_PORT:-10025}
PASS=0; FAIL=0

RUN=$(mktemp -d /tmp/tdv.XXXXXX)   # short: Unix socket paths are limited
mkdir -p "$D"/{state,home/dana/Maildir/{new,cur,tmp},work}
echo "dana:{PLAIN}secret" > "$D/users"
if [ -n "${TLS:-}" ]; then
  # servers on TLS-only ports, stunnel in front as on the Pi
  openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj /CN=localhost \
    -addext subjectAltName=DNS:localhost -keyout "$D/key.pem" -out "$D/cert.pem" 2>/dev/null
  chmod 644 "$D/key.pem"
  S_IMAP=0; S_POP3=0; S_SMTP=$((SMTP + 440))
  sed -e "s|@RUN@|$RUN|g" -e "s|@DIR@|$D|g" -e "s|@IMAP@|0|" -e "s|@POP3@|0|" \
      -e "s|@UID@|$(id -u nobody)|" -e "s|@GID@|$(id -g nobody)|" \
      -e "s|^ssl = no|ssl = yes\nssl_cert = <$D/cert.pem\nssl_key = <$D/key.pem|" \
      "$ROOT/tests/dovecot.conf.in" > "$D/dovecot.conf"
  python3 - "$D/dovecot.conf" $((IMAP + 850)) $((POP3 + 885)) <<'PY'
import sys
p, imaps, pop3s = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(p).read()
s = s.replace("inet_listener imaps {\n    port = 0", "inet_listener imaps {\n    port = " + imaps)
s = s.replace("inet_listener pop3s {\n    port = 0", "inet_listener pop3s {\n    port = " + pop3s)
open(p, "w").write(s)
PY
  cat > "$D/stunnel.conf" <<CONF
foreground = yes
pid =
sslVersionMin = TLSv1.2
verifyChain = yes
CAfile = $D/cert.pem
[imap]
client = yes
accept = 127.0.0.1:$IMAP
connect = localhost:$((IMAP + 850))
checkHost = localhost
[smtp]
client = yes
accept = 127.0.0.1:$SMTP
connect = localhost:$S_SMTP
checkHost = localhost
[pop3]
client = yes
accept = 127.0.0.1:$POP3
connect = localhost:$((POP3 + 885))
checkHost = localhost
CONF
  stunnel "$D/stunnel.conf" 2>"$D/stunnel.log" & TUNPID=$!
else
  S_SMTP=$SMTP
  sed -e "s|@RUN@|$RUN|g" -e "s|@DIR@|$D|g" -e "s|@IMAP@|$IMAP|" -e "s|@POP3@|$POP3|" \
      -e "s|@UID@|$(id -u nobody)|" -e "s|@GID@|$(id -g nobody)|" "$ROOT/tests/dovecot.conf.in" > "$D/dovecot.conf"
  TUNPID=
fi
chmod 755 "$D"
dovecot -c "$D/dovecot.conf" || { echo "can't start dovecot"; exit 1; }
if [ -n "${TLS:-}" ]; then
  python3 "$ROOT/tests/smtp_server.py" $S_SMTP "$D" dana secret "$D/cert.pem" "$D/key.pem" & SMTPPID=$!
else
  python3 "$ROOT/tests/smtp_server.py" $S_SMTP "$D" dana secret & SMTPPID=$!
fi
trap 'kill $SMTPPID $TUNPID 2>/dev/null; doveadm -c "$D/dovecot.conf" stop 2>/dev/null; rm -rf "$RUN"; [ -z "${KEEP:-}" ] && rm -rf "$D"' EXIT
sleep 1

# three messages waiting in the inbox: Hebrew multipart, windows-1255, HTML only
cp "$ROOT"/tests/mail/*.eml "$D/home/dana/Maildir/new/"
chown -R nobody: "$D/home"   # Dovecot won't serve mail as root

W=$D/work
cat > "$W/MAIL.INF" <<INF
[options]
tz=180
headers=300
[account]
name=Test IMAP
fullname=Yaary
email=dana@test.local
in=imap
host=127.0.0.1
port=$IMAP
user=dana
pass=secret
smtphost=127.0.0.1
smtpport=$SMTP
signature=Sent from my Atari Falcon
[account]
name=Test POP
email=dana@test.local
in=pop3
host=127.0.0.1
port=$POP3
user=dana
pass=secret
leave=1
smtphost=127.0.0.1
smtpport=$SMTP
INF

run() { ASAN_OPTIONS=detect_leaks=0 "$CLI" "$W" "$@" 2>&1; }
expect() {  # expect DESCRIPTION PATTERN OUTPUT
  if grep -q -- "$2" <<<"$3"; then PASS=$((PASS+1)); echo "ok   $1"
  else FAIL=$((FAIL+1)); echo "FAIL $1"; echo "     wanted: $2"; sed 's/^/     | /' <<<"$3"; fi
}
uid_of() { awk -F'\t' -v s="$2" 'index($0, s) { print $1; exit }' <<<"$1"; }

out=$(run folders 1)
expect "IMAP folder list has Sent with its role" "^Sent .*role=2" "$out"
expect "IMAP folder list has Trash with its role" "^Trash .*role=3" "$out"
expect "local Outbox exists" "^Outbox .*role=7 local=1" "$out"

out=$(run check 1)
expect "check finds 3 new messages" "new: 3" "$out"
out=$(run list 1 INBOX)
expect "Hebrew subject in the index" "שלום" "$out"
expect "windows-1255 subject decoded" "מכתב" "$out"
expect "attachment marker on the multipart message" "@" "$out"
U1=$(uid_of "$out" "שלום")
out=$(run show 1 INBOX "$U1")
expect "plain text part shown (not HTML)" "שלום 1990" "$out"
expect "attachment with RFC 2231 Hebrew name" "Attachment: ת.png" "$out"
out=$(run list 1 INBOX)
expect "reading marks the message seen" "^$U1	 " "$out"
U3=$(uid_of "$out" "Newsletter")
out=$(run show 1 INBOX "$U3")
expect "HTML-only message turned into text" "Hello Atari fans" "$out"
expect "HTML list item" "  - Falcon030" "$out"

run flag 1 INBOX "$U1" +flagged >/dev/null
run sync 1 INBOX >/dev/null
out=$(run list 1 INBOX)
expect "flag stored on the server and synced back" "^$U1	 !" "$out"

cat > "$W/msg.txt" <<'MSG'
To: Dana <dana@test.local>
Cc:
Bcc: dana@test.local
Subject: תשובה from the Falcon

שלום דנה,
this was written in MAIL.
.a line starting with a dot
MSG
out=$(run send 1 "$W/msg.txt")
expect "message sent over SMTP" "sent: 1" "$out"
out=$(run check 1)
expect "the sent message arrives (To + Bcc)" "new: 2" "$out"
out=$(run list 1 INBOX)
expect "Hebrew subject of the new mail" "תשובה from the Falcon" "$out"
U4=$(uid_of "$out" "תשובה")
out=$(run show 1 INBOX "$U4")
expect "Hebrew body arrives intact" "שלום דנה," "$out"
expect "dot-stuffing round trip" "^\.a line starting with a dot" "$out"
if grep -rq "X-Mail-Bcc" "$D/home/dana/Maildir"; then FAIL=$((FAIL+1)); echo "FAIL Bcc header leaked"; else PASS=$((PASS+1)); echo "ok   Bcc header not sent"; fi
out=$(run sync 1 Sent)
expect "copy saved in Sent with APPEND" "synced: 1 total" "$out"

out=$(run delete 1 INBOX "$U3")
expect "delete" "ok" "$out"
out=$(run sync 1 Trash)
expect "deleted message went to Trash" "synced: 1 total" "$out"
out=$(run list 1 INBOX)
if grep -q "Newsletter" <<<"$out"; then FAIL=$((FAIL+1)); echo "FAIL deleted message still listed"; else PASS=$((PASS+1)); echo "ok   deleted message gone from the index"; fi

out=$(run mkdir 1 "ארכיון")
expect "create a folder with a Hebrew name" "ok" "$out"
out=$(run folders 1)
expect "Hebrew folder name decoded from modified UTF-7" "disp=ארכיון" "$out"
SERVER=$(grep "disp=ארכיון" <<<"$out" | awk '{print $1}')
out=$(run move 1 INBOX "$U1" "$SERVER")
expect "move to the Hebrew folder" "ok" "$out"
out=$(run sync 1 "$SERVER")
expect "message is in the Hebrew folder" "synced: 1 total" "$out"

# a message expunged by another client disappears from the mirror
doveadm -c "$D/dovecot.conf" expunge -u dana mailbox INBOX header Message-ID "cp1255@example.org" || echo "doveadm expunge failed"
out=$(run sync 1 INBOX)
out=$(run list 1 INBOX)
if grep -q "מכתב" <<<"$out"; then FAIL=$((FAIL+1)); echo "FAIL expunged message still mirrored"; else PASS=$((PASS+1)); echo "ok   expunge by another client mirrored"; fi

out=$(run clearcache)
out=$(run show 1 INBOX "$U4")
expect "body downloaded again after clearing the cache" "שלום דנה," "$out"

out=$(run check 2)
expect "POP3 downloads the inbox" "new: 2" "$out"
out=$(run check 2)
expect "POP3 UIDL: nothing new the second time" "new: 0" "$out"
out=$(run list 2 Inbox)
expect "POP3 message in the local inbox" "תשובה" "$out"
P1=$(uid_of "$out" "תשובה")
out=$(run show 2 Inbox "$P1")
expect "POP3 message readable" "שלום דנה," "$out"
out=$(run delete 2 Inbox "$P1")
out=$(run list 2 Trash)
expect "POP3 delete goes to the local Trash" "תשובה" "$out"

echo "$PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
