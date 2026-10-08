/*
 * compose.c - building outgoing mail from the editor's text.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "plat.h"
#include "compose.h"
#include "charset.h"
#include "util.h"

#define VERSION_UA "MAIL 0.1 (Atari ST/Falcon)"

int addr_list(const char *s, char out[][160], int max)
{
	int n = 0;
	short q = 0, a = 0;
	const char *start = s;
	for (;; s++) {
		if (*s == '"')
			q = !q;
		else if (*s == '<')
			a = 1;
		else if (*s == '>')
			a = 0;
		if (!*s || ((*s == ',' || *s == ';') && !q && !a)) {
			long l = s - start;
			if (n < max && l > 0) {
				char tmp[160], *t;
				str_copy(tmp, start, l + 1 < 160 ? l + 1 : 160);
				t = str_trim(tmp);
				if (*t)
					str_copy(out[n++], t, 160);
			}
			if (!*s)
				break;
			start = s + 1;
		}
	}
	return n;
}

/* "Name <a@b>" (Atari) -> header form with an encoded or quoted name */
static void addr_encode(SBUF *b, const char *addr)
{
	char name[96], email[96];
	addr_split(addr, name, sizeof(name), email, sizeof(email));
	if (!*name) {
		sb_adds(b, email);
		return;
	}
	if (has_8bit(name, strlen(name))) {
		char *e = hdr_encode(name);
		sb_adds(b, e);
		free(e);
	} else if (strpbrk(name, "()<>@,;:\\\".[]")) {
		char *p;
		sb_addc(b, '"');
		for (p = name; *p; p++) {
			if (*p == '"' || *p == '\\')
				sb_addc(b, '\\');
			sb_addc(b, *p);
		}
		sb_addc(b, '"');
	} else {
		sb_adds(b, name);
	}
	sb_adds(b, " <");
	sb_adds(b, email);
	sb_addc(b, '>');
}

static void add_addr_header(SBUF *b, const char *name, const char *list)
{
	char addrs[40][160];
	int n = addr_list(list, addrs, 40), i;
	if (!n)
		return;
	sb_adds(b, name);
	sb_adds(b, ": ");
	for (i = 0; i < n; i++) {
		if (i)
			sb_adds(b, ",\r\n ");
		addr_encode(b, addrs[i]);
	}
	sb_adds(b, "\r\n");
}

static unsigned long days_civil(long y, short m, short d)
{
	long era, yoe, doy, doe;
	y -= m <= 2;
	era = y / 400;
	yoe = y - era * 400;
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return (unsigned long)(era * 146097 + doe - 719468);
}

static void date_header(char *out, int size)
{
	static const char *wd[] = { "Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed" };
	static const char *mn[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug",
				    "Sep", "Oct", "Nov", "Dec" };
	PFTIME t;
	long tz = opt.tz;
	pf_now(&t);
	snprintf(out, size, "%s, %d %s %d %02d:%02d:%02d %c%02ld%02ld",
		 wd[days_civil(t.year, t.mon, t.day) % 7], t.day, mn[(t.mon + 11) % 12], t.year,
		 t.hour, t.min, t.sec, tz < 0 ? '-' : '+', labs(tz) / 60, labs(tz) % 60);
}

static const char *mime_type(const char *name)
{
	static const char *map[][2] = {
		{ ".JPG", "image/jpeg" }, { ".JPEG", "image/jpeg" }, { ".PNG", "image/png" },
		{ ".GIF", "image/gif" }, { ".PDF", "application/pdf" }, { ".TXT", "text/plain" },
		{ ".ZIP", "application/zip" }, { ".EML", "message/rfc822" }, { ".HTM", "text/html" },
		{ ".HTML", "text/html" }, { ".DOC", "application/msword" }, { ".MP3", "audio/mpeg" },
		{ ".WAV", "audio/wav" }, { ".PRG", "application/octet-stream" }, { 0, 0 }
	};
	const char *dot = strrchr(name, '.');
	short i;
	if (dot)
		for (i = 0; map[i][0]; i++)
			if (!strcasecmp(dot, map[i][0]))
				return map[i][1];
	return "application/octet-stream";
}

static const char *base_name(const char *path)
{
	const char *p = strrchr(path, '\\'), *q = strrchr(path, '/');
	if (q > p)
		p = q;
	return p ? p + 1 : path;
}

/* the editor's header value for `name`, joined over continuation lines */
static char *field(const char *text, long hl, const char *name)
{
	return mime_header(text, hl, name);
}

char *compose_build(ACCOUNT *a, const char *text, long len, const char *in_reply_to,
		    const char *references, char *err, int errlen, long *outlen)
{
	SBUF b;
	long hl = mime_header_len(text, len), bl;
	char *to = field(text, hl, "To"), *cc = field(text, hl, "Cc"), *bcc = field(text, hl, "Bcc");
	char *subj = field(text, hl, "Subject"), *body, *qp, *enc_subj;
	char date[48], boundary[48], from[200];
	const char *b8 = "7bit";
	char attach[8][160];
	short nattach = 0, i;
	PFTIME t;
	const char *p;
	int body8, longline = 0;

	/* every Attach: line */
	for (p = text; p < text + hl; ) {
		const char *e = memchr(p, '\n', text + hl - p);
		long l = e ? e - p : text + hl - p;
		if (l > 7 && !strncasecmp(p, "Attach:", 7) && nattach < 8) {
			char tmp[160], *v;
			str_copy(tmp, p + 7, l - 7 + 1 < 160 ? l - 7 + 1 : 160);
			v = str_trim(tmp);
			if (*v)
				str_copy(attach[nattach++], v, 160);
		}
		p = e ? e + 1 : text + hl;
	}

	if ((!to || !*to) && (!cc || !*cc) && (!bcc || !*bcc)) {
		str_copy(err, "Please give at least one recipient on the To: line.", errlen);
		free(to);
		free(cc);
		free(bcc);
		free(subj);
		return 0;
	}

	sb_init(&b);
	date_header(date, sizeof(date));
	pf_now(&t);
	snprintf(from, sizeof(from), "%s <%s>", a->fullname, a->email);
	sb_printf(&b, "Date: %s\r\n", date);
	sb_adds(&b, "From: ");
	if (a->fullname[0])
		addr_encode(&b, from);
	else
		sb_adds(&b, a->email);
	sb_adds(&b, "\r\n");
	if (to)
		add_addr_header(&b, "To", to);
	if (cc)
		add_addr_header(&b, "Cc", cc);
	if (bcc)
		add_addr_header(&b, "X-Mail-Bcc", bcc);
	enc_subj = hdr_encode(subj ? subj : "");
	sb_printf(&b, "Subject: ");
	sb_adds(&b, enc_subj);
	sb_adds(&b, "\r\n");
	free(enc_subj);
	{
		const char *dom = strchr(a->email, '@');
		sb_printf(&b, "Message-ID: <%04d%02d%02d%02d%02d%02d.%lx.mail@%s>\r\n", t.year, t.mon, t.day,
			  t.hour, t.min, t.sec, pf_ms() & 0xFFFFFF, dom ? dom + 1 : "atari.local");
	}
	if (in_reply_to && *in_reply_to)
		sb_printf(&b, "In-Reply-To: %s\r\n", in_reply_to);
	if (references && *references) {
		sb_adds(&b, "References: ");
		sb_adds(&b, references);
		sb_adds(&b, "\r\n");
	}
	sb_adds(&b, "User-Agent: " VERSION_UA "\r\nMIME-Version: 1.0\r\n");

	/* body: Atari -> UTF-8, quoted-printable when not plain ASCII */
	{
		const char *bs = text + hl;
		long bn = len - hl, i2, col = 0;
		body = atari_to_utf8(bs, bn, &bl);
		body8 = has_8bit(body, bl);
		for (i2 = 0; i2 < bl; i2++) {
			if (body[i2] == '\n')
				col = 0;
			else if (++col > 900)
				longline = 1;
		}
		if (body8 || longline) {
			qp = qp_encode(body, bl, &bl);
			free(body);
			body = qp;
			b8 = "quoted-printable";
		} else {
			/* CRLF line ends */
			SBUF c;
			sb_init(&c);
			for (i2 = 0; i2 < bl; i2++) {
				if (body[i2] == '\n')
					sb_addc(&c, '\r');
				if (body[i2] != '\r')
					sb_addc(&c, body[i2]);
			}
			free(body);
			bl = c.len;
			body = sb_steal(&c);
		}
	}

	if (!nattach) {
		sb_printf(&b, "Content-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: %s\r\n\r\n", b8);
		sb_add(&b, body, bl);
		if (bl < 2 || body[bl - 1] != '\n')
			sb_adds(&b, "\r\n");
	} else {
		snprintf(boundary, sizeof(boundary), "=_mail_%lx_%d%02d", pf_ms(), t.min, t.sec);
		sb_printf(&b, "Content-Type: multipart/mixed; boundary=\"%s\"\r\n\r\n", boundary);
		sb_adds(&b, "This is a multi-part message in MIME format.\r\n");
		sb_printf(&b, "--%s\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: %s\r\n\r\n",
			  boundary, b8);
		sb_add(&b, body, bl);
		if (bl < 2 || body[bl - 1] != '\n')
			sb_adds(&b, "\r\n");
		for (i = 0; i < nattach; i++) {
			long fl, el;
			char *data = pf_load(attach[i], &fl), *enc, *fname;
			const char *type = mime_type(attach[i]);
			if (!data) {
				snprintf(err, errlen, "Can't read the attachment %s", attach[i]);
				sb_free(&b);
				free(body);
				free(to);
				free(cc);
				free(bcc);
				free(subj);
				return 0;
			}
			fname = hdr_encode(base_name(attach[i]));
			if (!strcmp(type, "message/rfc822")) {
				sb_printf(&b, "--%s\r\nContent-Type: message/rfc822\r\nContent-Disposition: attachment; filename=\"%s\"\r\n\r\n",
					  boundary, "forwarded.eml");
				sb_add(&b, data, fl);
				if (fl < 2 || data[fl - 1] != '\n')
					sb_adds(&b, "\r\n");
			} else {
				enc = base64_encode(data, fl, 76, &el);
				sb_printf(&b, "--%s\r\nContent-Type: %s; name=\"%s\"\r\nContent-Transfer-Encoding: base64\r\n"
					  "Content-Disposition: attachment; filename=\"%s\"\r\n\r\n",
					  boundary, type, fname, fname);
				sb_add(&b, enc, el);
				sb_adds(&b, "\r\n");
				free(enc);
			}
			free(fname);
			free(data);
		}
		sb_printf(&b, "--%s--\r\n", boundary);
	}
	free(body);
	free(to);
	free(cc);
	free(bcc);
	free(subj);
	if (outlen)
		*outlen = b.len;
	return sb_steal(&b);
}

int compose_rcpts(const char *raw, long len, char rcpt[][96], int max)
{
	static const char *hdrs[] = { "To", "Cc", "X-Mail-Bcc", 0 };
	long hl = mime_header_len(raw, len);
	int n = 0;
	short k;
	for (k = 0; hdrs[k]; k++) {
		char *v = mime_header(raw, hl, hdrs[k]), list[40][160];
		int c, i;
		if (!v)
			continue;
		c = addr_list(v, list, 40);
		for (i = 0; i < c && n < max; i++) {
			addr_split(list[i], 0, 0, rcpt[n], 96);
			if (strchr(rcpt[n], '@'))
				n++;
		}
		free(v);
	}
	return n;
}

char *compose_for_smtp(const char *raw, long len, long *outlen)
{
	long hl = mime_header_len(raw, len), i = 0;
	SBUF b;
	sb_init(&b);
	while (i < hl) {
		long e = i;
		int skip = !strncasecmp(raw + i, "X-Mail-Bcc:", sizeof("X-Mail-Bcc:") - 1);
		do {
			while (e < hl && raw[e] != '\n')
				e++;
			e++;
		} while (e < hl && (raw[e] == ' ' || raw[e] == '\t'));
		if (!skip)
			sb_add(&b, raw + i, e - i);
		i = e;
	}
	sb_add(&b, raw + hl, len - hl);
	if (outlen)
		*outlen = b.len;
	return sb_steal(&b);
}

static void add_signature(SBUF *b, ACCOUNT *a)
{
	if (a->signature[0]) {
		sb_adds(b, "\n\n-- \n");
		sb_adds(b, a->signature);
	}
}

char *compose_new(ACCOUNT *a, const char *to)
{
	SBUF b;
	sb_init(&b);
	sb_printf(&b, "To: %s\nCc: \nSubject: \n\n", to ? to : "");
	add_signature(&b, a);
	return sb_steal(&b);
}

static void quote_text(SBUF *b, const char *text)
{
	const char *p = text;
	while (*p) {
		const char *e = strchr(p, '\n');
		long l = e ? e - p : (long)strlen(p);
		sb_adds(b, *p == '>' ? ">" : "> ");
		sb_add(b, p, l);
		sb_addc(b, '\n');
		p = e ? e + 1 : p + l;
	}
}

static int is_me(ACCOUNT *a, const char *addr)
{
	char email[96];
	addr_split(addr, 0, 0, email, sizeof(email));
	return !strcasecmp(email, a->email);
}

char *compose_reply(ACCOUNT *a, MSG *m, int all)
{
	SBUF b;
	const char *to = m->reply_to[0] ? m->reply_to : m->from;
	sb_init(&b);
	sb_adds(&b, "To: ");
	sb_adds(&b, to);
	sb_adds(&b, "\nCc: ");
	if (all) {
		char list[40][160];
		int n = 0, i, first = 1;
		n = addr_list(m->to, list, 20);
		n += addr_list(m->cc, list + n, 20);
		for (i = 0; i < n; i++) {
			if (is_me(a, list[i]) || str_istr(to, list[i]))
				continue;
			if (!first)
				sb_adds(&b, ", ");
			sb_adds(&b, list[i]);
			first = 0;
		}
	}
	sb_adds(&b, "\nSubject: ");
	if (!str_istarts(m->subject, "Re:"))
		sb_adds(&b, "Re: ");
	sb_adds(&b, m->subject);
	/* an empty line for the cursor above the quote */
	sb_printf(&b, "\n\n\n\nOn %s,\n%s wrote:\n", m->date, m->from);
	quote_text(&b, m->text ? m->text : "");
	add_signature(&b, a);
	return sb_steal(&b);
}

char *compose_forward(ACCOUNT *a, MSG *m, const char *eml)
{
	SBUF b;
	sb_init(&b);
	sb_adds(&b, "To: \nCc: \nSubject: ");
	if (!str_istarts(m->subject, "Fwd:"))
		sb_adds(&b, "Fwd: ");
	sb_adds(&b, m->subject);
	if (eml && m->nparts)
		sb_printf(&b, "\nAttach: %s", eml);
	sb_adds(&b, "\n\n---------- Forwarded message ----------\n");
	sb_printf(&b, "From: %s\nDate: %s\nSubject: %s\nTo: %s\n\n", m->from, m->date, m->subject, m->to);
	sb_adds(&b, m->text ? m->text : "");
	add_signature(&b, a);
	return sb_steal(&b);
}
