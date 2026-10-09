/*
 * smtp.c - SMTP submission client.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "smtp.h"
#include "charset.h"

/* read a (multi-line) reply; returns the code, -1 on error */
static int reply(SMTP *s, int collect)
{
	int code;
	if (collect)
		s->ext[0] = 0;
	for (;;) {
		if (conn_getline(s->c, &s->line) < 0) {
			str_copy(s->err, s->c->err, sizeof(s->err));
			return -1;
		}
		code = atoi(s->line.s);
		if (collect && s->line.len > 4) {
			long l = strlen(s->ext), i;
			for (i = 4; i < s->line.len && l < (long)sizeof(s->ext) - 3; i++)
				s->ext[l++] = (char)toupper((unsigned char)s->line.s[i]);
			s->ext[l++] = '\n';
			s->ext[l] = 0;
		}
		if (s->line.len < 4 || s->line.s[3] != '-')
			break;
	}
	str_copy(s->err, s->line.s, sizeof(s->err));
	return code;
}

static int has_auth(SMTP *s, const char *mech)
{
	const char *a = strstr(s->ext, "AUTH");
	long ml = strlen(mech);
	while (a) {
		const char *e = strchr(a, '\n'), *p = a + 4;
		while (p && *p && p < e) {
			while (*p == ' ' || *p == '=')
				p++;
			if (!strncmp(p, mech, ml) && (p[ml] == ' ' || p[ml] == '\n' || !p[ml]))
				return 1;
			while (*p && *p != ' ' && *p != '\n')
				p++;
		}
		a = e ? strstr(e, "AUTH") : 0;
	}
	return 0;
}

static SMTP *open_once(const char *host, unsigned short port, int sec, const char *helo,
		       const char *user, const char *pass, char *err, int errlen)
{
	SMTP *s = calloc(1, sizeof(SMTP));
	int code;
	if (!s) {
		str_copy(err, "out of memory", errlen);
		return 0;
	}
	sb_init(&s->line);
	s->c = conn_open("SMTP", host, port, sec, err, errlen);
	if (!s->c) {
		free(s);
		return 0;
	}
	s->c->timeout_ms = 120000;
	if (reply(s, 0) != 220) {
		snprintf(err, errlen, "%s: %s", host, s->err);
		smtp_quit(s);
		return 0;
	}
	conn_cmd(s->c, 0, "EHLO %s", helo);
	code = reply(s, 1);
	if (sec == SEC_STARTTLS) {
		if (code != 250 || !strstr(s->ext, "STARTTLS")) {
			snprintf(err, errlen, "%s doesn't offer STARTTLS: use port 465", host);
			smtp_quit(s);
			return 0;
		}
		conn_cmd(s->c, 0, "STARTTLS");
		if (reply(s, 0) != 220) {
			snprintf(err, errlen, "STARTTLS refused: %s", s->err);
			smtp_quit(s);
			return 0;
		}
		if (!conn_starttls(s->c, host, err, errlen)) {
			conn_close(s->c);
			s->c = 0;
			smtp_quit(s);
			return 0;
		}
		/* start over inside TLS */
		conn_cmd(s->c, 0, "EHLO %s", helo);
		code = reply(s, 1);
	}
	if (code != 250) {
		conn_cmd(s->c, 0, "HELO %s", helo);
		if (reply(s, 0) != 250) {
			snprintf(err, errlen, "%s", s->err);
			smtp_quit(s);
			return 0;
		}
	}
	if (user && *user) {
		long ul = strlen(user), pl = strlen(pass);
		char raw[300], *b64;
		conn_log_login("SMTP", user, pass);
		if (has_auth(s, "PLAIN") || !has_auth(s, "LOGIN")) {
			if (ul + pl + 2 > (long)sizeof(raw))
				pl = sizeof(raw) - ul - 2;
			raw[0] = 0;
			memcpy(raw + 1, user, ul + 1);
			memcpy(raw + 2 + ul, pass, pl);
			b64 = base64_encode(raw, ul + pl + 2, 0, 0);
			conn_cmd(s->c, 1, "AUTH PLAIN %s", b64);
			free(b64);
			code = reply(s, 0);
		} else {
			conn_cmd(s->c, 0, "AUTH LOGIN");
			code = reply(s, 0);
			if (code == 334) {
				b64 = base64_encode(user, ul, 0, 0);
				conn_cmd(s->c, 1, "%s", b64);
				free(b64);
				code = reply(s, 0);
			}
			if (code == 334) {
				b64 = base64_encode(pass, pl, 0, 0);
				conn_cmd(s->c, 1, "%s", b64);
				free(b64);
				code = reply(s, 0);
			}
		}
		if (code != 235) {
			snprintf(err, errlen, "SMTP login failed: %s", s->err);
			smtp_quit(s);
			return 0;
		}
	}
	return s;
}

SMTP *smtp_open(const char *host, unsigned short port, int sec, const char *helo,
		const char *user, const char *pass, char *err, int errlen)
{
	SMTP *s = open_once(host, port, sec, helo, user, pass, err, errlen);
	if (!s && sec == SEC_STARTTLS && conn_tls_retry)
		s = open_once(host, port, sec, helo, user, pass, err, errlen);
	return s;
}

int smtp_send(SMTP *s, const char *from, const char **rcpts, int n,
	      const char *data, long len)
{
	int i, ok = 0;
	long a, b;
	SBUF out;

	conn_cmd(s->c, 0, "MAIL FROM:<%s>", from);
	if (reply(s, 0) != 250)
		return 0;
	for (i = 0; i < n; i++) {
		int code;
		conn_cmd(s->c, 0, "RCPT TO:<%s>", rcpts[i]);
		code = reply(s, 0);
		if (code == 250 || code == 251)
			ok++;
		else if (code < 0)
			return -1;
	}
	if (!ok) {
		conn_cmd(s->c, 0, "RSET");
		reply(s, 0);
		return 0;
	}
	conn_cmd(s->c, 0, "DATA");
	if (reply(s, 0) != 354)
		return 0;

	/* CRLF line ends and dot-stuffing, sent in 8 KB pieces */
	sb_init(&out);
	conn_log("SMTP", " >> ", "[message]", 9);
	for (a = 0; a < len; a = b) {
		for (b = a; b < len && data[b] != '\n'; b++)
			;
		if (data[a] == '.')
			sb_addc(&out, '.');
		sb_add(&out, data + a, (b > a && data[b - 1] == '\r') ? b - a - 1 : b - a);
		sb_add(&out, "\r\n", 2);
		if (b < len)
			b++;
		if (out.len > 8000) {
			if (!conn_write(s->c, out.s, out.len)) {
				sb_free(&out);
				str_copy(s->err, s->c->err, sizeof(s->err));
				return -1;
			}
			sb_reset(&out);
		}
	}
	sb_adds(&out, ".\r\n");
	i = conn_write(s->c, out.s, out.len);
	sb_free(&out);
	if (!i) {
		str_copy(s->err, s->c->err, sizeof(s->err));
		return -1;
	}
	return reply(s, 0) == 250 ? 1 : 0;
}

void smtp_quit(SMTP *s)
{
	if (!s)
		return;
	if (s->c) {
		conn_cmd(s->c, 0, "QUIT");
		s->c->timeout_ms = 3000;
		reply(s, 0);
		conn_close(s->c);
	}
	sb_free(&s->line);
	free(s);
}
