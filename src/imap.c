/*
 * imap.c - IMAP4rev1 client.
 *
 * Each response is read whole, with any {n} literals inlined after
 * their marker, then tokenised. Commands are synchronous, like the GFA
 * GFA Troll's: send, then read until the tagged completion.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include "imap.h"
#include "charset.h"
#include "util.h"

typedef unsigned char u8;

enum { T_END, T_ATOM, T_QUOTED, T_LITERAL, T_NIL, T_LP, T_RP };

typedef struct {
	short type;
	const char *s;
	long n;
} TOK;

static void next_tok(const char **pp, const char *end, TOK *t)
{
	const char *p = *pp;
	while (p < end && (*p == ' ' || *p == '\r' || *p == '\n'))
		p++;
	t->s = p;
	t->n = 0;
	if (p >= end) {
		t->type = T_END;
	} else if (*p == '(') {
		t->type = T_LP;
		p++;
	} else if (*p == ')') {
		t->type = T_RP;
		p++;
	} else if (*p == '"') {
		const char *s = ++p;
		while (p < end && *p != '"') {
			if (*p == '\\' && p + 1 < end)
				p++;
			p++;
		}
		t->type = T_QUOTED;
		t->s = s;
		t->n = p - s;
		if (p < end)
			p++;
	} else if (*p == '{') {
		long n = strtol(p + 1, 0, 10);
		while (p < end && *p != '\n')
			p++;
		p++;
		if (p + n > end)
			n = end - p;
		t->type = T_LITERAL;
		t->s = p;
		t->n = n;
		p += n;
	} else {
		const char *s = p;
		while (p < end && *p != ' ' && *p != '(' && *p != ')' && *p != '\r' && *p != '\n') {
			if (*p == '[') {	/* BODY[HEADER.FIELDS (A B)] */
				while (p < end && *p != ']')
					p++;
			}
			if (p < end)
				p++;
		}
		t->type = (p - s == 3 && !strncasecmp(s, "NIL", 3)) ? T_NIL : T_ATOM;
		t->s = s;
		t->n = p - s;
	}
	*pp = p;
}

/* copy a token's string value (unescaping quoted strings) */
static void tok_str(const TOK *t, char *out, long size)
{
	long i, o = 0;
	if (t->type == T_NIL || t->type == T_END || t->type == T_LP || t->type == T_RP) {
		*out = 0;
		return;
	}
	for (i = 0; i < t->n && o < size - 1; i++) {
		if (t->type == T_QUOTED && t->s[i] == '\\' && i + 1 < t->n)
			i++;
		out[o++] = t->s[i];
	}
	out[o] = 0;
}

static void skip_value(const char **pp, const char *end, TOK *t)
{
	short depth;
	if (t->type != T_LP)
		return;
	for (depth = 1; depth > 0; ) {
		next_tok(pp, end, t);
		if (t->type == T_END)
			return;
		if (t->type == T_LP)
			depth++;
		else if (t->type == T_RP)
			depth--;
	}
}

void imap_quote(char *out, int size, const char *s)
{
	int o = 0;
	out[o++] = '"';
	while (*s && o < size - 3) {
		if (*s == '"' || *s == '\\')
			out[o++] = '\\';
		out[o++] = *s++;
	}
	out[o++] = '"';
	out[o] = 0;
}

unsigned short imap_parse_flags(const char *s, long n)
{
	unsigned short f = 0;
	const char *e = s + n;
	while (s < e) {
		const char *w;
		long l;
		while (s < e && (*s == ' ' || *s == '('))
			s++;
		w = s;
		while (s < e && *s != ' ' && *s != ')')
			s++;
		l = s - w;
		if (l == 5 && !strncasecmp(w, "\\Seen", 5)) f |= MF_SEEN;
		else if (l == 9 && !strncasecmp(w, "\\Answered", 9)) f |= MF_ANSWERED;
		else if (l == 8 && !strncasecmp(w, "\\Flagged", 8)) f |= MF_FLAGGED;
		else if (l == 8 && !strncasecmp(w, "\\Deleted", 8)) f |= MF_DELETED;
		else if (l == 6 && !strncasecmp(w, "\\Draft", 6)) f |= MF_DRAFT;
		else if (l == 10 && !strncasecmp(w, "$Forwarded", 10)) f |= MF_FORWARDED;
		else if (l == 5 && !strncasecmp(w, "$Junk", 5)) f |= MF_JUNK;
		if (s < e && *s == ')')
			s++;
	}
	return f;
}

static void flags_str(char *out, unsigned short f)
{
	*out = 0;
	strcat(out, "(");
	if (f & MF_SEEN) strcat(out, "\\Seen ");
	if (f & MF_ANSWERED) strcat(out, "\\Answered ");
	if (f & MF_FLAGGED) strcat(out, "\\Flagged ");
	if (f & MF_DELETED) strcat(out, "\\Deleted ");
	if (f & MF_DRAFT) strcat(out, "\\Draft ");
	if (f & MF_FORWARDED) strcat(out, "$Forwarded ");
	if (f & MF_JUNK) strcat(out, "$Junk ");
	if (out[strlen(out) - 1] == ' ')
		out[strlen(out) - 1] = 0;
	strcat(out, ")");
}

/* read one full response (with literals) into im->resp */
static int read_response(IMAP *im)
{
	SBUF line;
	sb_init(&line);
	sb_reset(&im->resp);
	for (;;) {
		long n = conn_getline(im->c, &line), lit;
		const char *br;
		if (n < 0) {
			str_copy(im->err, im->c->err, sizeof(im->err));
			sb_free(&line);
			return -1;
		}
		sb_add(&im->resp, line.s, line.len);
		/* a line ending in {n} (or {n+}) is followed by n bytes */
		if (n < 3 || line.s[n - 1] != '}' || !(br = strrchr(line.s, '{'))) {
			sb_free(&line);
			if (!im->resp.s)
				sb_add(&im->resp, "", 0);
			return 1;
		}
		lit = strtol(br + 1, 0, 10);
		sb_adds(&im->resp, "\r\n");
		if (!conn_getbytes(im->c, &im->resp, lit)) {
			str_copy(im->err, im->c->err, sizeof(im->err));
			sb_free(&line);
			return -1;
		}
	}
}

static void note_caps(IMAP *im, const char *s)
{
	const char *p = str_istr(s, "CAPABILITY ");
	long i = 1;
	if (!p)
		return;
	p += 11;
	im->caps[0] = ' ';
	while (*p && *p != ']' && i < (long)sizeof(im->caps) - 2)
		im->caps[i++] = (char)toupper((u8)*p++);
	im->caps[i++] = ' ';
	im->caps[i] = 0;
}

int imap_has(IMAP *im, const char *cap)
{
	char k[40];
	snprintf(k, sizeof(k), " %s ", cap);
	return strstr(im->caps, k) != 0;
}

/* generic untagged data every command may bring */
static void untagged(IMAP *im, const char *s)
{
	const char *p;
	if (str_istr(s, "CAPABILITY "))
		note_caps(im, s);
	if ((p = str_istr(s, "[UIDVALIDITY ")))
		im->uidvalidity = strtoul(p + 13, 0, 10);
	if ((p = str_istr(s, "[UIDNEXT ")))
		im->uidnext = strtoul(p + 9, 0, 10);
	if (str_istr(s, "[READ-ONLY]"))
		im->readonly = 1;
	if (isdigit((u8)s[2])) {
		char *e;
		unsigned long n = strtoul(s + 2, &e, 10);
		if (!strncasecmp(e, " EXISTS", 7))
			im->exists = n;
	}
}

typedef void (*UNTAGGED)(IMAP *im, void *ud, const char *s, long n);

static int run(IMAP *im, int secret, UNTAGGED cb, void *ud, const char *fmt, ...)
{
	char cmd[700], tag[8];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(cmd, sizeof(cmd), fmt, ap);
	va_end(ap);
	snprintf(tag, sizeof(tag), "T%04u", ++im->tag);
	if (!conn_cmd(im->c, secret, "%s %s", tag, cmd)) {
		str_copy(im->err, im->c->err, sizeof(im->err));
		return -1;
	}
	for (;;) {
		const char *s;
		if (read_response(im) < 0)
			return -1;
		s = im->resp.s;
		if (!strncmp(s, tag, 5) && s[5] == ' ') {
			if (!strncasecmp(s + 6, "OK", 2)) {
				untagged(im, s);
				return 1;
			}
			str_copy(im->err, s + 6, sizeof(im->err));
			return 0;
		}
		if (s[0] == '*' && s[1] == ' ') {
			untagged(im, s);
			if (cb)
				cb(im, ud, s, im->resp.len);
		} else if (s[0] == '+') {
			/* unexpected continuation: cancel with an empty line */
			conn_write(im->c, "\r\n", 2);
		}
	}
}

static IMAP *login_once(const char *host, unsigned short port, int sec, const char *user,
			const char *pass, char *err, int errlen)
{
	IMAP *im = calloc(1, sizeof(IMAP));
	int r;
	if (!im) {
		str_copy(err, "out of memory", errlen);
		return 0;
	}
	sb_init(&im->resp);
	im->delim = '/';
	im->c = conn_open("IMAP", host, port, sec, err, errlen);
	if (!im->c) {
		free(im);
		return 0;
	}
	if (read_response(im) < 0 || strncmp(im->resp.s, "* ", 2)) {
		snprintf(err, errlen, "%s did not greet as an IMAP server", host);
		imap_logout(im);
		return 0;
	}
	if (str_istarts(im->resp.s, "* BYE")) {
		str_copy(err, im->resp.s + 6, errlen);
		imap_logout(im);
		return 0;
	}
	note_caps(im, im->resp.s);
	if (!im->caps[0] && run(im, 0, 0, 0, "CAPABILITY") < 0) {
		str_copy(err, im->err, errlen);
		imap_logout(im);
		return 0;
	}
	if (sec == SEC_STARTTLS) {
		if (!imap_has(im, "STARTTLS")) {
			snprintf(err, errlen, "%s doesn't offer STARTTLS: use port 993", host);
			imap_logout(im);
			return 0;
		}
		if (run(im, 0, 0, 0, "STARTTLS") <= 0) {
			snprintf(err, errlen, "STARTTLS refused: %s", im->err);
			imap_logout(im);
			return 0;
		}
		if (!conn_starttls(im->c, host, err, errlen)) {
			conn_close(im->c);	/* no LOGOUT on a broken handshake */
			im->c = 0;
			imap_logout(im);
			return 0;
		}
		/* what was said before TLS can't be trusted: ask again */
		im->caps[0] = 0;
		if (run(im, 0, 0, 0, "CAPABILITY") < 0) {
			str_copy(err, im->err, errlen);
			imap_logout(im);
			return 0;
		}
	}

	if (imap_has(im, "AUTH=PLAIN") && (imap_has(im, "LOGINDISABLED") || has_8bit(pass, strlen(pass)) ||
					   has_8bit(user, strlen(user)))) {
		/* SASL PLAIN copes with any characters in the password */
		char raw[300], *b64;
		long ul = strlen(user), pl = strlen(pass);
		if (ul + pl + 2 > (long)sizeof(raw))
			pl = sizeof(raw) - ul - 2;
		raw[0] = 0;
		memcpy(raw + 1, user, ul);
		raw[1 + ul] = 0;
		memcpy(raw + 2 + ul, pass, pl);
		b64 = base64_encode(raw, ul + pl + 2, 0, 0);
		if (imap_has(im, "SASL-IR")) {
			r = run(im, 2, 0, 0, "AUTHENTICATE PLAIN %s", b64);
		} else {
			char tag[8];
			snprintf(tag, sizeof(tag), "T%04u", ++im->tag);
			conn_cmd(im->c, 0, "%s AUTHENTICATE PLAIN", tag);
			r = -1;
			if (read_response(im) > 0 && im->resp.s[0] == '+') {
				conn_cmd(im->c, 1, "%s", b64);
				while ((r = read_response(im)) > 0) {
					if (!strncmp(im->resp.s, tag, 5)) {
						r = !strncasecmp(im->resp.s + 6, "OK", 2);
						if (!r)
							str_copy(im->err, im->resp.s + 6, sizeof(im->err));
						else
							untagged(im, im->resp.s);
						break;
					}
					untagged(im, im->resp.s);
				}
			}
		}
		free(b64);
	} else {
		char qu[200], qp[200];
		imap_quote(qu, sizeof(qu), user);
		imap_quote(qp, sizeof(qp), pass);
		r = run(im, 2, 0, 0, "LOGIN %s %s", qu, qp);
	}
	if (r <= 0) {
		snprintf(err, errlen, "login failed: %s", im->err);
		imap_logout(im);
		return 0;
	}
	/* some servers only list their full capabilities after login */
	if (!imap_has(im, "IMAP4REV1") || !strstr(im->caps, "UIDPLUS"))
		run(im, 0, 0, 0, "CAPABILITY");
	return im;
}

IMAP *imap_login(const char *host, unsigned short port, int sec, const char *user,
		 const char *pass, char *err, int errlen)
{
	IMAP *im = login_once(host, port, sec, user, pass, err, errlen);
	/* a server with only an ECDSA certificate: once more, accepting it */
	if (!im && sec == SEC_STARTTLS && conn_tls_retry)
		im = login_once(host, port, sec, user, pass, err, errlen);
	return im;
}

void imap_logout(IMAP *im)
{
	if (!im)
		return;
	if (im->c) {
		char tag[8];
		snprintf(tag, sizeof(tag), "T%04u", ++im->tag);
		conn_cmd(im->c, 0, "%s LOGOUT", tag);
		im->c->timeout_ms = 3000;
		while (read_response(im) > 0 && strncmp(im->resp.s, tag, 5))
			;
		conn_close(im->c);
	}
	sb_free(&im->resp);
	free(im);
}

/* ---- LIST ---- */

typedef struct {
	void (*cb)(void *, const char *, char, int, int);
	void *ud;
} LISTCB;

static void list_cb(IMAP *im, void *ud, const char *s, long n)
{
	LISTCB *l = ud;
	const char *p = s + 2, *end = s + n;
	TOK t;
	char name[300], delim = '/', fl[200] = "";
	int role = 0, noselect;

	next_tok(&p, end, &t);
	if (t.type != T_ATOM || t.n != 4 || strncasecmp(t.s, "LIST", 4))
		return;
	next_tok(&p, end, &t);		/* (flags) */
	if (t.type == T_LP) {
		const char *fs = p;
		skip_value(&p, end, &t);
		str_copy(fl, fs, (p - fs) < (long)sizeof(fl) ? (p - fs) : (long)sizeof(fl));
	}
	next_tok(&p, end, &t);		/* delimiter */
	if (t.type == T_QUOTED && t.n)
		delim = t.s[t.n - 1];
	next_tok(&p, end, &t);		/* name */
	tok_str(&t, name, sizeof(name));
	if (!*name)
		return;
	im->delim = delim;
	if (!strcasecmp(name, "INBOX")) role = FR_INBOX;
	else if (str_istr(fl, "\\Sent")) role = FR_SENT;
	else if (str_istr(fl, "\\Trash")) role = FR_TRASH;
	else if (str_istr(fl, "\\Drafts")) role = FR_DRAFTS;
	else if (str_istr(fl, "\\Junk")) role = FR_JUNK;
	else if (str_istr(fl, "\\Archive")) role = FR_ARCHIVE;
	noselect = str_istr(fl, "\\Noselect") || str_istr(fl, "\\NonExistent");
	l->cb(l->ud, name, delim, role, noselect);
}

int imap_list(IMAP *im, void (*cb)(void *, const char *, char, int, int), void *ud)
{
	LISTCB l;
	l.cb = cb;
	l.ud = ud;
	return run(im, 0, list_cb, &l, "LIST \"\" \"*\"");
}

int imap_select(IMAP *im, const char *mbox, int readonly)
{
	char q[300];
	int r;
	imap_quote(q, sizeof(q), mbox);
	im->exists = im->uidvalidity = im->uidnext = 0;
	im->readonly = 0;
	r = run(im, 0, 0, 0, "%s %s", readonly ? "EXAMINE" : "SELECT", q);
	if (r > 0)
		str_copy(im->selected, mbox, sizeof(im->selected));
	else
		im->selected[0] = 0;
	return r;
}

/* ---- FETCH ---- */

typedef struct {
	void (*cb)(void *, IMAPFETCH *);
	void *ud;
} FETCHCB;

static void fetch_cb(IMAP *im, void *ud, const char *s, long n)
{
	FETCHCB *fc = ud;
	const char *p = s + 2, *end = s + n;
	IMAPFETCH f;
	TOK t;

	memset(&f, 0, sizeof(f));
	next_tok(&p, end, &t);
	if (t.type != T_ATOM)
		return;
	f.seq = strtoul(t.s, 0, 10);
	next_tok(&p, end, &t);
	if (t.type != T_ATOM || t.n != 5 || strncasecmp(t.s, "FETCH", 5))
		return;
	next_tok(&p, end, &t);
	if (t.type != T_LP)
		return;
	for (;;) {
		char key[64];
		next_tok(&p, end, &t);
		if (t.type != T_ATOM)
			break;
		tok_str(&t, key, sizeof(key));
		next_tok(&p, end, &t);
		if (!strcasecmp(key, "UID")) {
			f.uid = strtoul(t.s, 0, 10);
		} else if (!strcasecmp(key, "RFC822.SIZE")) {
			f.size = strtoul(t.s, 0, 10);
		} else if (!strcasecmp(key, "FLAGS") && t.type == T_LP) {
			const char *fs = p;
			skip_value(&p, end, &t);
			f.flags = imap_parse_flags(fs, p - fs);
		} else if (!strcasecmp(key, "INTERNALDATE")) {
			tok_str(&t, f.internaldate, sizeof(f.internaldate));
		} else if (!strncasecmp(key, "BODY[", 5) || !strncasecmp(key, "RFC822", 6)) {
			if (t.type == T_LITERAL || t.type == T_QUOTED) {
				if (str_istr(key, "HEADER")) {
					f.hdr = t.s;
					f.hdrlen = t.n;
				} else {
					f.body = t.s;
					f.bodylen = t.n;
				}
			}
		} else {
			skip_value(&p, end, &t);
		}
	}
	(void)im;
	if (f.uid)
		fc->cb(fc->ud, &f);
}

int imap_fetch(IMAP *im, const char *uidset, const char *items,
	       void (*cb)(void *, IMAPFETCH *), void *ud)
{
	FETCHCB fc;
	fc.cb = cb;
	fc.ud = ud;
	return run(im, 0, fetch_cb, &fc, "UID FETCH %s %s", uidset, items);
}

int imap_fetch_seq(IMAP *im, const char *seqset, const char *items,
		   void (*cb)(void *, IMAPFETCH *), void *ud)
{
	FETCHCB fc;
	fc.cb = cb;
	fc.ud = ud;
	return run(im, 0, fetch_cb, &fc, "FETCH %s %s", seqset, items);
}

typedef struct {
	long *messages, *unseen;
} STATUSCB;

static void status_cb(IMAP *im, void *ud, const char *s, long n)
{
	STATUSCB *st = ud;
	const char *p;
	(void)im;
	(void)n;
	if (!str_istarts(s + 2, "STATUS "))
		return;
	if ((p = str_istr(s, "MESSAGES ")))
		*st->messages = strtol(p + 9, 0, 10);
	if ((p = str_istr(s, "UNSEEN ")))
		*st->unseen = strtol(p + 7, 0, 10);
}

int imap_status(IMAP *im, const char *mbox, long *messages, long *unseen)
{
	char q[300];
	STATUSCB st;
	st.messages = messages;
	st.unseen = unseen;
	imap_quote(q, sizeof(q), mbox);
	return run(im, 0, status_cb, &st, "STATUS %s (MESSAGES UNSEEN)", q);
}

int imap_store(IMAP *im, const char *uidset, int add, unsigned short flags)
{
	char fs[120];
	flags_str(fs, flags);
	return run(im, 0, 0, 0, "UID STORE %s %cFLAGS.SILENT %s", uidset, add ? '+' : '-', fs);
}

int imap_store_seq(IMAP *im, const char *seqset, int add, unsigned short flags)
{
	char fs[120];
	flags_str(fs, flags);
	return run(im, 0, 0, 0, "STORE %s %cFLAGS.SILENT %s", seqset, add ? '+' : '-', fs);
}

int imap_copy(IMAP *im, const char *uidset, const char *dest)
{
	char q[300];
	imap_quote(q, sizeof(q), dest);
	return run(im, 0, 0, 0, "UID COPY %s %s", uidset, q);
}

int imap_expunge(IMAP *im, const char *uidset)
{
	if (imap_has(im, "UIDPLUS"))
		return run(im, 0, 0, 0, "UID EXPUNGE %s", uidset);
	return run(im, 0, 0, 0, "EXPUNGE");
}

int imap_move(IMAP *im, const char *uidset, const char *dest)
{
	char q[300];
	int r;
	if (imap_has(im, "MOVE")) {
		imap_quote(q, sizeof(q), dest);
		return run(im, 0, 0, 0, "UID MOVE %s %s", uidset, q);
	}
	r = imap_copy(im, uidset, dest);
	if (r <= 0)
		return r;
	r = imap_store(im, uidset, 1, MF_DELETED);
	if (r <= 0)
		return r;
	return imap_expunge(im, uidset);
}

int imap_append(IMAP *im, const char *mbox, unsigned short flags, const char *data, long len)
{
	char q[300], fs[120], tag[8];
	imap_quote(q, sizeof(q), mbox);
	flags_str(fs, flags);
	snprintf(tag, sizeof(tag), "T%04u", ++im->tag);
	if (!conn_cmd(im->c, 0, "%s APPEND %s %s {%ld}", tag, q, fs, len))
		return -1;
	if (read_response(im) < 0)
		return -1;
	if (im->resp.s[0] != '+') {
		str_copy(im->err, im->resp.s, sizeof(im->err));
		return 0;
	}
	conn_log("IMAP", " >> ", "[message]", 9);
	if (!conn_write(im->c, data, len) || !conn_write(im->c, "\r\n", 2))
		return -1;
	for (;;) {
		if (read_response(im) < 0)
			return -1;
		if (!strncmp(im->resp.s, tag, 5)) {
			if (!strncasecmp(im->resp.s + 6, "OK", 2))
				return 1;
			str_copy(im->err, im->resp.s + 6, sizeof(im->err));
			return 0;
		}
		untagged(im, im->resp.s);
	}
}

int imap_create(IMAP *im, const char *mbox)
{
	char q[300];
	imap_quote(q, sizeof(q), mbox);
	return run(im, 0, 0, 0, "CREATE %s", q);
}

int imap_delete(IMAP *im, const char *mbox)
{
	char q[300];
	imap_quote(q, sizeof(q), mbox);
	return run(im, 0, 0, 0, "DELETE %s", q);
}

int imap_noop(IMAP *im)
{
	return run(im, 0, 0, 0, "NOOP");
}
