/*
 * mime.c - reading internet messages (RFC 5322 + MIME).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "mime.h"
#include "charset.h"
#include "util.h"

typedef unsigned char u8;

long mime_header_len(const char *raw, long len)
{
	long i;
	if (len >= 2 && raw[0] == '\r' && raw[1] == '\n')
		return 2;
	if (len >= 1 && raw[0] == '\n')
		return 1;
	for (i = 0; i + 1 < len; i++) {
		if (raw[i] != '\n')
			continue;
		if (raw[i + 1] == '\n')
			return i + 2;
		if (raw[i + 1] == '\r' && i + 2 < len && raw[i + 2] == '\n')
			return i + 3;
	}
	return len;
}

char *mime_header(const char *hdr, long n, const char *name)
{
	long nl = strlen(name), i = 0;
	while (i < n) {
		/* at the start of a line */
		if (i + nl < n && hdr[i + nl] == ':' && !strncasecmp(hdr + i, name, nl)) {
			SBUF b;
			long j = i + nl + 1;
			sb_init(&b);
			for (;;) {
				long s = j;
				while (j < n && hdr[j] != '\n')
					j++;
				sb_add(&b, hdr + s, j - s);
				if (b.len && b.s[b.len - 1] == '\r')
					b.s[--b.len] = 0;
				if (j + 1 < n && (hdr[j + 1] == ' ' || hdr[j + 1] == '\t')) {
					j++;	/* folded line: continue */
					continue;
				}
				break;
			}
			{
				char *t = str_trim(b.s ? b.s : (char *)"");
				char *r = strdup(t);
				sb_free(&b);
				return r;
			}
		}
		while (i < n && hdr[i] != '\n')
			i++;
		i++;
	}
	return 0;
}

char *mime_param(const char *value, const char *param)
{
	const char *p = value;
	long pl = strlen(param);
	if (!value)
		return 0;
	while ((p = strchr(p, ';'))) {
		const char *s, *e;
		int star = 0;
		p++;
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
			p++;
		if (strncasecmp(p, param, pl))
			continue;
		s = p + pl;
		if (*s == '*') {	/* RFC 2231: name*=utf-8''%D7%A9... (or name*0=) */
			star = 1;
			s++;
			if (*s == '0') {
				s++;
				if (*s == '*')
					s++;
				else
					star = 0;
			}
		}
		while (*s == ' ')
			s++;
		if (*s != '=')
			continue;
		s++;
		while (*s == ' ')
			s++;
		if (*s == '"') {
			SBUF b;
			sb_init(&b);
			for (s++; *s && *s != '"'; s++) {
				if (*s == '\\' && s[1])
					s++;
				sb_addc(&b, *s);
			}
			return sb_steal(&b);
		}
		e = s;
		while (*e && *e != ';' && *e != ' ' && *e != '\t' && *e != '\r' && *e != '\n')
			e++;
		if (star) {
			/* charset'lang'percent-encoded */
			const char *q1 = memchr(s, '\'', e - s), *q2;
			char cs[32] = "us-ascii", *raw, *out;
			long rl = 0;
			if (q1 && (q2 = memchr(q1 + 1, '\'', e - q1 - 1))) {
				str_copy(cs, s, (q1 - s + 1) < 32 ? (q1 - s + 1) : 32);
				s = q2 + 1;
			}
			raw = malloc(e - s + 1);
			if (!raw)
				return 0;
			while (s < e) {
				if (*s == '%' && s + 2 < e + 0 + 1 && isxdigit((u8)s[1]) && isxdigit((u8)s[2])) {
					char h[3] = { s[1], s[2], 0 };
					raw[rl++] = (char)strtoul(h, 0, 16);
					s += 3;
				} else {
					raw[rl++] = *s++;
				}
			}
			out = cs_to_atari(raw, rl, cs_id(cs), 0);
			free(raw);
			return out;
		}
		return str_ndup(s, e - s);
	}
	return 0;
}

/* ---------------- dates ---------------- */

static const char months[] = "janfebmaraprmayjunjulaugsepoctnovdec";

static unsigned long days_from_civil(long y, short m, short d)
{
	long era, yoe, doy, doe;
	y -= m <= 2;
	era = y / 400;
	yoe = y - era * 400;
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return (unsigned long)(era * 146097 + doe - 719468);
}

unsigned long mime_date(const char *s)
{
	long day = 0, year = 0, hh = 0, mm = 0, ss = 0, tz = 0;
	short mon = 0, i;
	const char *p = s;
	char *e;
	if (!s)
		return 0;
	while (*p && !isdigit((u8)*p))	/* skip "Tue, " */
		p++;
	day = strtol(p, &e, 10);
	p = e;
	while (*p == ' ' || *p == '-')
		p++;
	for (i = 0; i < 12; i++)
		if (!strncasecmp(p, months + i * 3, 3))
			mon = i + 1;
	if (!mon)
		return 0;
	p += 3;
	while (*p == ' ' || *p == '-')
		p++;
	year = strtol(p, &e, 10);
	if (e == p)
		return 0;
	if (year < 50)
		year += 2000;
	else if (year < 100)
		year += 1900;
	p = e;
	while (*p == ' ')
		p++;
	hh = strtol(p, &e, 10);
	if (*e == ':') {
		mm = strtol(e + 1, &e, 10);
		if (*e == ':')
			ss = strtol(e + 1, &e, 10);
	}
	p = e;
	while (*p == ' ')
		p++;
	if (*p == '+' || *p == '-') {
		long z = strtol(p + 1, 0, 10);
		tz = (z / 100) * 60 + z % 100;
		if (*p == '-')
			tz = -tz;
	} else if (!strncasecmp(p, "EDT", 3)) {
		tz = -240;
	} else if (!strncasecmp(p, "EST", 3) || !strncasecmp(p, "CDT", 3)) {
		tz = !strncasecmp(p, "EST", 3) ? -300 : -300;
	} else if (!strncasecmp(p, "PDT", 3)) {
		tz = -420;
	} else if (!strncasecmp(p, "PST", 3)) {
		tz = -480;
	}
	if (year < 1970 || day < 1 || day > 31)
		return 0;
	return days_from_civil(year, mon, (short)day) * 86400UL + hh * 3600 + mm * 60 + ss - tz * 60;
}

/* ---------------- addresses ---------------- */

void addr_split(const char *addr, char *name, int nsize, char *email, int esize)
{
	const char *lt = strchr(addr, '<'), *gt;
	if (name)
		*name = 0;
	if (email)
		*email = 0;
	if (lt && (gt = strchr(lt, '>'))) {
		if (email) {
			long n = gt - lt - 1;
			str_copy(email, lt + 1, n + 1 < esize ? n + 1 : esize);
		}
		if (name) {
			long n = lt - addr;
			char *t;
			str_copy(name, addr, n + 1 < nsize ? n + 1 : nsize);
			t = str_trim(name);
			if (*t == '"') {
				t++;
				if (*t && t[strlen(t) - 1] == '"')
					t[strlen(t) - 1] = 0;
			}
			memmove(name, t, strlen(t) + 1);
		}
	} else {
		char tmp[256], *t;
		str_copy(tmp, addr, sizeof(tmp));
		t = str_trim(tmp);
		/* "a@b (Name)" */
		if (strchr(t, '(')) {
			char *o = strchr(t, '('), *c = strchr(o, ')');
			if (name && c) {
				*c = 0;
				str_copy(name, o + 1, nsize);
			}
			*o = 0;
			t = str_trim(t);
		}
		if (email)
			str_copy(email, t, esize);
	}
}

/* ---------------- HTML ---------------- */

static const struct {
	const char *name;
	unsigned short u;
} entities[] = {
	{ "nbsp", 0xA0 }, { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' },
	{ "apos", '\'' }, { "copy", 0xA9 }, { "reg", 0xAE }, { "trade", 0x2122 },
	{ "hellip", 0x2026 }, { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "rsquo", 0x2019 },
	{ "lsquo", 0x2018 }, { "rdquo", 0x201D }, { "ldquo", 0x201C }, { "bull", 0x2022 },
	{ "middot", 0xB7 }, { "euro", 0x20AC }, { "pound", 0xA3 }, { "laquo", 0xAB },
	{ "raquo", 0xBB }, { "eacute", 0xE9 }, { "egrave", 0xE8 }, { "agrave", 0xE0 },
	{ "uuml", 0xFC }, { "ouml", 0xF6 }, { "auml", 0xE4 }, { "szlig", 0xDF },
	{ "ccedil", 0xE7 }, { "zwnj", 0x200C }, { "rlm", 0x200F }, { "lrm", 0x200E },
	{ "shy", 0x00AD }, { "times", 0xD7 }, { "deg", 0xB0 }, { 0, 0 }
};

static int tag_is(const char *t, long tl, const char *name)
{
	long n = strlen(name);
	return tl >= n && !strncasecmp(t, name, n) && (tl == n || !isalnum((u8)t[n]));
}

char *html_to_text(const char *h, long n, long *outlen)
{
	SBUF b;
	long i = 0;
	short skip = 0, pre = 0, space = 0, nl = 2;
	sb_init(&b);

	while (i < n) {
		u8 c = (u8)h[i];
		if (c == '<') {
			long j = i + 1, tl;
			const char *t;
			short close = 0;
			if (i + 3 < n && !strncmp(h + i, "<!--", 4)) {
				const char *e = 0;
				long k;
				for (k = i + 4; k + 2 < n; k++)
					if (h[k] == '-' && h[k + 1] == '-' && h[k + 2] == '>') {
						e = h + k;
						break;
					}
				i = e ? (e - h) + 3 : n;
				continue;
			}
			while (j < n && h[j] != '>')
				j++;
			t = h + i + 1;
			if (*t == '/') {
				close = 1;
				t++;
			}
			tl = (h + j) - t;
			i = j + 1;
			if (tag_is(t, tl, "style") || tag_is(t, tl, "script") || tag_is(t, tl, "head") ||
			    tag_is(t, tl, "title")) {
				skip = close ? 0 : 1;
				continue;
			}
			if (skip)
				continue;
			if (tag_is(t, tl, "pre")) {
				pre = !close;
				continue;
			}
			if (tag_is(t, tl, "br") || (close && (tag_is(t, tl, "div") || tag_is(t, tl, "tr") ||
			    tag_is(t, tl, "li") || tag_is(t, tl, "table")))) {
				if (nl < 1 || tag_is(t, tl, "br")) {
					sb_addc(&b, '\n');
					nl++;
				}
				space = 0;
				continue;
			}
			if (tag_is(t, tl, "p") || (tag_is(t, tl, "h1") || tag_is(t, tl, "h2") ||
			    tag_is(t, tl, "h3") || tag_is(t, tl, "h4") || tag_is(t, tl, "blockquote"))) {
				while (nl < 2) {
					sb_addc(&b, '\n');
					nl++;
				}
				space = 0;
				continue;
			}
			if (!close && tag_is(t, tl, "li")) {
				if (nl < 1)
					sb_addc(&b, '\n');
				sb_adds(&b, "  - ");
				nl = 0;
				space = 0;
				continue;
			}
			if (!close && (tag_is(t, tl, "td") || tag_is(t, tl, "th")) && nl == 0) {
				sb_addc(&b, ' ');
				continue;
			}
			if (!close && tag_is(t, tl, "hr")) {
				if (nl < 1)
					sb_addc(&b, '\n');
				sb_adds(&b, "----------------------------------------\n");
				nl = 1;
				continue;
			}
			continue;
		}
		if (skip) {
			i++;
			continue;
		}
		if (c == '&') {
			char out[4];
			short k = 0, ok = 0;
			unsigned long u = 0;
			long j = i + 1;
			if (j < n && h[j] == '#') {
				j++;
				if (j < n && (h[j] == 'x' || h[j] == 'X'))
					u = strtoul(h + j + 1, 0, 16);
				else
					u = strtoul(h + j, 0, 10);
				while (j < n && h[j] != ';' && j - i < 10)
					j++;
				ok = j < n && h[j] == ';';
			} else {
				short e;
				for (e = 0; entities[e].name; e++) {
					long l = strlen(entities[e].name);
					if (j + l < n && !strncmp(h + j, entities[e].name, l) && h[j + l] == ';') {
						u = entities[e].u;
						j += l;
						ok = 1;
						break;
					}
				}
			}
			if (ok) {
				i = j + 1;
				if (u == 0xA0)
					u = ' ';
				k = (short)uni_to_atari(u, out);
				if (space && nl == 0)
					sb_addc(&b, ' ');
				space = 0;
				if (k)
					sb_add(&b, out, k), nl = 0;
				continue;
			}
		}
		if (!pre && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
			space = 1;
			i++;
			continue;
		}
		if (space && nl == 0)
			sb_addc(&b, ' ');
		space = 0;
		sb_addc(&b, (char)c);
		nl = c == '\n' ? nl + 1 : 0;
		i++;
	}
	/* trim trailing blank lines */
	while (b.len && (b.s[b.len - 1] == '\n' || b.s[b.len - 1] == ' '))
		b.s[--b.len] = 0;
	if (outlen)
		*outlen = b.len;
	return sb_steal(&b);
}

/* ---------------- structure ---------------- */

typedef struct {
	const char *raw;
	MSG *m;
	SBUF text;
	short have_plain, have_html;
} WALK;

static char *decode_body(const char *body, long n, short enc, long *outlen)
{
	if (enc == ENC_BASE64)
		return base64_decode(body, n, outlen);
	if (enc == ENC_QP)
		return qp_decode(body, n, 0, outlen);
	*outlen = n;
	return str_ndup(body, n);
}

static void add_text(WALK *w, const char *body, long n, short enc, const char *charset, int html)
{
	long dl, al, tl;
	char *dec = decode_body(body, n, enc, &dl), *at, *txt, *s, *d;
	if (!dec)
		return;
	at = cs_to_atari(dec, dl, cs_id(charset), &al);
	free(dec);
	if (!at)
		return;
	if (html) {
		txt = html_to_text(at, al, &tl);
		free(at);
		w->m->html = 1;
	} else {
		txt = at;
		tl = al;
	}
	if (!txt)
		return;
	/* drop CRs */
	for (s = d = txt; s < txt + tl; s++)
		if (*s != '\r')
			*d++ = *s;
	tl = d - txt;
	if (w->text.len && tl)
		sb_adds(&w->text, "\n\n");
	sb_add(&w->text, txt, tl);
	free(txt);
}

static void add_part(WALK *w, const char *name, const char *type, long off, long len, short enc)
{
	MIMEPART *p;
	if (w->m->nparts >= MIME_MAXPARTS)
		return;
	p = &w->m->parts[w->m->nparts++];
	p->name = strdup(name && *name ? name : "attachment");
	p->type = strdup(type);
	p->off = off;
	p->len = len;
	p->enc = enc;
	p->size = enc == ENC_BASE64 ? len * 3 / 4 : len;
}

static void walk(WALK *w, long start, long end, int depth, int in_alt);

static void walk_multipart(WALK *w, long start, long end, const char *boundary, int depth, int alt)
{
	char delim[90];
	long dl, i, part_start = -1;
	const char *r = w->raw;
	long best_s = -1, best_e = -1;
	short best_html = 2;

	snprintf(delim, sizeof(delim), "--%s", boundary);
	dl = strlen(delim);
	for (i = start; i < end; ) {
		long eol = i;
		while (eol < end && r[eol] != '\n')
			eol++;
		if (eol - i >= dl && !strncmp(r + i, delim, dl)) {
			int last = (eol - i >= dl + 2 && r[i + dl] == '-' && r[i + dl + 1] == '-');
			if (part_start >= 0) {
				long pe = i;
				/* the CRLF before the delimiter belongs to it */
				if (pe > part_start && r[pe - 1] == '\n')
					pe--;
				if (pe > part_start && r[pe - 1] == '\r')
					pe--;
				if (alt) {
					/* multipart/alternative: remember the best text part */
					long hl = mime_header_len(r + part_start, pe - part_start);
					char *ct = mime_header(r + part_start, hl, "Content-Type");
					short is_html = ct && str_istr(ct, "text/html") ? 1 : 0;
					short is_plain = !ct || str_istr(ct, "text/plain");
					short is_multi = ct && str_istr(ct, "multipart/");
					if (is_multi || is_plain || is_html) {
						short rank = is_plain ? 0 : is_multi ? 0 : 1;
						if (rank < best_html || best_s < 0) {
							best_html = rank;
							best_s = part_start;
							best_e = pe;
						}
					}
					free(ct);
				} else {
					walk(w, part_start, pe, depth + 1, 0);
				}
			}
			if (last) {
				part_start = -1;
				break;
			}
			part_start = eol + 1;
		}
		i = eol + 1;
	}
	if (part_start >= 0 && part_start < end && !alt)	/* unterminated last part */
		walk(w, part_start, end, depth + 1, 0);
	if (alt && best_s >= 0)
		walk(w, best_s, best_e, depth + 1, 1);
}

static void walk(WALK *w, long start, long end, int depth, int in_alt)
{
	const char *r = w->raw + start;
	long n = end - start, hl = mime_header_len(r, n);
	char *ct = mime_header(r, hl, "Content-Type");
	char *cte = mime_header(r, hl, "Content-Transfer-Encoding");
	char *cd = mime_header(r, hl, "Content-Disposition");
	char type[64] = "text/plain";
	short enc = ENC_7BIT;
	int attach;
	(void)in_alt;

	if (ct) {
		long k = strcspn(ct, "; \t\r\n");
		str_copy(type, ct, k + 1 < 64 ? k + 1 : 64);
		for (k = 0; type[k]; k++)
			type[k] = (char)tolower((u8)type[k]);
	}
	if (cte) {
		if (str_istarts(cte, "base64"))
			enc = ENC_BASE64;
		else if (str_istarts(cte, "quoted-printable"))
			enc = ENC_QP;
	}
	attach = cd && str_istarts(cd, "attachment");

	if (!strncmp(type, "multipart/", 10) && depth < 8) {
		char *bnd = mime_param(ct, "boundary");
		if (bnd) {
			walk_multipart(w, start + hl, end, bnd, depth,
				       !strcmp(type, "multipart/alternative"));
			free(bnd);
		}
	} else if (!attach && (!strcmp(type, "text/plain") || !strcmp(type, "text/html"))) {
		char *cs = mime_param(ct, "charset");
		add_text(w, r + hl, n - hl, enc, cs ? cs : "us-ascii", type[5] == 'h');
		free(cs);
	} else {
		char *name = cd ? mime_param(cd, "filename") : 0;
		if (!name && ct)
			name = mime_param(ct, "name");
		if (name && strstr(name, "=?")) {
			char *d = hdr_decode(name);
			free(name);
			name = d;
		}
		if (!name && !strcmp(type, "message/rfc822"))
			name = strdup("message.eml");
		add_part(w, name, type, start + hl, n - hl, enc);
		free(name);
	}
	free(ct);
	free(cte);
	free(cd);
}

static char *dec_hdr(const char *raw, long hl, const char *name)
{
	char *v = mime_header(raw, hl, name), *d;
	if (!v)
		return strdup("");
	d = hdr_decode(v);
	free(v);
	return d ? d : strdup("");
}

static char *raw_hdr(const char *raw, long hl, const char *name)
{
	char *v = mime_header(raw, hl, name);
	return v ? v : strdup("");
}

MSG *mime_parse(const char *raw, long len)
{
	MSG *m = calloc(1, sizeof(MSG));
	WALK w;
	long hl;
	if (!m)
		return 0;
	hl = mime_header_len(raw, len);
	m->from = dec_hdr(raw, hl, "From");
	m->to = dec_hdr(raw, hl, "To");
	m->cc = dec_hdr(raw, hl, "Cc");
	m->reply_to = dec_hdr(raw, hl, "Reply-To");
	m->subject = dec_hdr(raw, hl, "Subject");
	m->date = dec_hdr(raw, hl, "Date");
	m->message_id = raw_hdr(raw, hl, "Message-ID");
	m->references = raw_hdr(raw, hl, "References");
	m->in_reply_to = raw_hdr(raw, hl, "In-Reply-To");
	m->list_post = raw_hdr(raw, hl, "List-Post");

	w.raw = raw;
	w.m = m;
	sb_init(&w.text);
	walk(&w, 0, len, 0, 0);
	m->textlen = w.text.len;
	m->text = sb_steal(&w.text);
	return m;
}

void mime_free(MSG *m)
{
	short i;
	if (!m)
		return;
	free(m->from);
	free(m->to);
	free(m->cc);
	free(m->reply_to);
	free(m->subject);
	free(m->date);
	free(m->message_id);
	free(m->references);
	free(m->in_reply_to);
	free(m->list_post);
	free(m->text);
	for (i = 0; i < m->nparts; i++) {
		free(m->parts[i].name);
		free(m->parts[i].type);
	}
	free(m);
}

char *mime_part_data(const char *raw, const MIMEPART *p, long *outlen)
{
	return decode_body(raw + p->off, p->len, p->enc, outlen);
}
