/*
 * unit.c - checks of the mail core that need no server: charsets
 * (Hebrew included), MIME decoding, message building, bidi, dates.
 * Built for the development machine by `make test`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "charset.h"
#include "mime.h"
#include "compose.h"
#include "store.h"
#include "bidi.h"
#include "util.h"
#include "plat.h"
#include "tls.h"

static int fails, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* Atari bytes for the Hebrew word shalom (shin lamed vav final-mem) */
static const char shalom_atari[] = "\xD6\xCD\xC7\xDA";
static const char shalom_utf8[] = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";

static void t_charset(void)
{
	char *s;
	long n;

	s = cs_to_atari(shalom_utf8, strlen(shalom_utf8), CS_UTF8, &n);
	CHECK(n == 4 && !memcmp(s, shalom_atari, 4), "utf8 hebrew -> atari got %ld bytes", n);
	free(s);

	/* ISO-8859-8 and windows-1255 shalom: F9 EC E5 ED */
	s = cs_to_atari("\xF9\xEC\xE5\xED", 4, CS_ISO8859_8, &n);
	CHECK(n == 4 && !memcmp(s, shalom_atari, 4), "iso-8859-8 hebrew");
	free(s);
	s = cs_to_atari("\xF9\xEC\xE5\xED", 4, CS_CP1255, &n);
	CHECK(n == 4 && !memcmp(s, shalom_atari, 4), "cp1255 hebrew");
	free(s);

	/* niqqud and direction marks disappear */
	s = cs_to_atari("\xD7\xA9\xD6\xB8\xE2\x80\x8F", 7, CS_UTF8, &n);
	CHECK(n == 1 && (unsigned char)s[0] == 0xD6, "niqqud dropped, got %ld", n);
	free(s);

	/* Latin-1 accents map to the ST font; curly quotes become ASCII */
	/* already Atari text, as a Troll-era bridge sends it, still labelled
	   UTF-8; first as if in reading order (bridgeorder=0) */
	cs_bridge_visual = 0;
	s = cs_to_atari("\xD6\xCD\xC7\xDA caf\x82", 9, CS_UTF8, &n);
	CHECK(n == 9 && !memcmp(s, "\xD6\xCD\xC7\xDA caf\x82", 9), "atari text labelled utf-8");
	free(s);
	s = cs_to_atari("\xD6\xCD\xC7\xDA", 4, CS_LATIN1, &n);
	CHECK(n == 4 && !memcmp(s, shalom_atari, 4), "atari text labelled latin-1");
	free(s);
	/* in display order, as the bridge sends it for Troll: turned back */
	cs_bridge_visual = 1;
	s = cs_to_atari("\xDA\xC7\xCD\xD6 Dana", 9, CS_UTF8, &n);
	CHECK(n == 9 && !memcmp(s, "Dana \xD6\xCD\xC7\xDA", 9), "bridge display order -> reading order");
	free(s);
	/* the Falcon mail proxy (whomper/atari_web): subject in "x" Q words
	   of 14 bytes, display order; "Re:" makes it left to right */
	{
		char *d = hdr_decode("=?x?Q?Re:_=DA=C7=CD=D6_=C6?= =?x?Q?=CF=C5?=");
		CHECK(d && !strcmp(d, "Re: \xC5\xCF\xC6 \xD6\xCD\xC7\xDA"), "proxy subject: %s", d ? d : "");
		free(d);
		/* a UTF-8 letter split between two words */
		d = hdr_decode("=?UTF-8?B?1w==?= =?UTF-8?B?qQ==?=");
		CHECK(d && !strcmp(d, "\xD6"), "utf-8 letter split across words");
		free(d);
	}
	s = cs_to_atari(".\xDA\xC7\xCD\xD6", 5, CS_ATARI_VISUAL, &n);
	CHECK(n == 5 && !memcmp(s, "\xD6\xCD\xC7\xDA.", 5), "x-atari-st body");
	free(s);
	/* real Latin-1 capitals stay Latin-1 */
	s = cs_to_atari("\xC7" "a a \xE9t\xE9 \xC9\xC9", 11, CS_LATIN1, &n);
	CHECK(n == 11 && (unsigned char)s[0] == 0x80 && (unsigned char)s[10] == 0x90, "latin-1 kept");
	free(s);
	s = cs_to_atari("caf\xC3\xA9 \xE2\x80\x9Cok\xE2\x80\x9D", 14, CS_UTF8, &n);
	CHECK(!strcmp(s, "caf\x82 \"ok\""), "accents/quotes: %s", s);
	free(s);

	s = atari_to_utf8(shalom_atari, 4, &n);
	CHECK(n == 8 && !memcmp(s, shalom_utf8, 8), "atari -> utf8 hebrew");
	free(s);

	s = hdr_decode("=?UTF-8?B?16nXnNeV150=?= =?UTF-8?Q?_world?=");
	CHECK(!strcmp(s, "\xD6\xCD\xC7\xDA world"), "rfc2047 decode: [%s]", s);
	free(s);

	s = hdr_decode("=?windows-1255?Q?=F9=EC=E5=ED?= from Tel Aviv");
	CHECK(!strcmp(s, "\xD6\xCD\xC7\xDA from Tel Aviv"), "rfc2047 cp1255: [%s]", s);
	free(s);

	s = hdr_encode("Shalom \xD6\xCD\xC7\xDA");
	CHECK(!strncmp(s, "=?UTF-8?B?", 10), "rfc2047 encode: %s", s);
	{
		char *back = hdr_decode(s);
		CHECK(!strcmp(back, "Shalom \xD6\xCD\xC7\xDA"), "encode round trip [%s]", back);
		free(back);
	}
	free(s);

	s = mutf7_to_atari("&BdIF3AXTBdk-");	/* gimel lamed dalet yod */
	CHECK(!strcmp(s, "\xC4\xCD\xC5\xCB"), "mutf7 decode [%s]", s);
	{
		char *e = atari_to_mutf7(s);
		CHECK(!strcmp(e, "&BdIF3AXTBdk-"), "mutf7 encode [%s]", e);
		free(e);
	}
	free(s);
	s = mutf7_to_atari("Tom &- Jerry");
	CHECK(!strcmp(s, "Tom & Jerry"), "mutf7 ampersand");
	free(s);

	s = qp_decode("a=3Db=\r\nc", 10, 0, &n);
	CHECK(!strcmp(s, "a=bc"), "qp decode [%s]", s);
	free(s);
	s = base64_decode("aGVsbG8=", 8, &n);
	CHECK(n == 5 && !strcmp(s, "hello"), "base64");
	free(s);
}

static const char *multipart =
	"From: =?UTF-8?B?15PXoNeU?= <dana@example.com>\r\n"
	"To: me@example.com\r\n"
	"Subject: =?UTF-8?B?16nXnNeV150=?=\r\n"
	"Date: Thu, 08 Oct 2026 18:30:00 +0300\r\n"
	"Message-ID: <abc@example.com>\r\n"
	"MIME-Version: 1.0\r\n"
	"Content-Type: multipart/mixed; boundary=\"XX\"\r\n"
	"\r\n"
	"preamble\r\n"
	"--XX\r\n"
	"Content-Type: multipart/alternative; boundary=YY\r\n"
	"\r\n"
	"--YY\r\n"
	"Content-Type: text/plain; charset=utf-8\r\n"
	"Content-Transfer-Encoding: base64\r\n"
	"\r\n"
	"16nXnNeV150gMTk5MA==\r\n"
	"--YY\r\n"
	"Content-Type: text/html; charset=utf-8\r\n"
	"\r\n"
	"<p>HTML version</p>\r\n"
	"--YY--\r\n"
	"--XX\r\n"
	"Content-Type: image/png; name=\"pic.png\"\r\n"
	"Content-Disposition: attachment; filename*=utf-8''%D7%AA.png\r\n"
	"Content-Transfer-Encoding: base64\r\n"
	"\r\n"
	"iVBORw0KGgo=\r\n"
	"--XX--\r\n";

static void t_mime(void)
{
	MSG *m = mime_parse(multipart, strlen(multipart));
	long n;
	char *d;
	CHECK(m != 0, "parse");
	CHECK(!strcmp(m->subject, shalom_atari), "subject [%s]", m->subject);
	CHECK(!strcmp(m->text, "\xD6\xCD\xC7\xDA 1990"), "plain part chosen: [%s]", m->text);
	CHECK(m->nparts == 1, "one attachment, got %d", m->nparts);
	CHECK(m->nparts && !strcmp(m->parts[0].name, "\xD7.png"), "rfc2231 filename [%s]",
	      m->nparts ? m->parts[0].name : "");
	d = mime_part_data(multipart, &m->parts[0], &n);
	CHECK(n == 8 && !memcmp(d, "\x89PNG\r\n\x1a\n", 8), "attachment data %ld", n);
	free(d);
	CHECK(mime_date("Thu, 08 Oct 2026 18:30:00 +0300") == 1791473400UL,
	      "date %lu", mime_date("Thu, 08 Oct 2026 18:30:00 +0300"));
	mime_free(m);

	{
		const char *h = "<html><head><style>p{}</style></head><body><p>Hello&nbsp;<b>you</b></p>"
				"<ul><li>one</li><li>&#1513;</li></ul>&lt;3</body></html>";
		char *t = html_to_text(h, strlen(h), &n);
		CHECK(!strcmp(t, "Hello you\n\n  - one\n  - \xD6\n<3"), "html: [%s]", t);
		free(t);
	}
	{
		char name[64], email[64];
		addr_split("\"Cohen, Dana\" <dana@example.com>", name, 64, email, 64);
		CHECK(!strcmp(name, "Cohen, Dana") && !strcmp(email, "dana@example.com"), "addr_split %s|%s", name, email);
		addr_split("avi@example.org (Avi)", name, 64, email, 64);
		CHECK(!strcmp(name, "Avi") && !strcmp(email, "avi@example.org"), "addr_split comment %s|%s", name, email);
	}
}

static void t_compose(void)
{
	ACCOUNT a;
	char err[200], rc[10][96];
	char *raw, *smtp, text[400];
	long n, sl;
	int k;
	MSG *m;
	memset(&a, 0, sizeof(a));
	strcpy(a.email, "me@example.com");
	strcpy(a.fullname, "Yaary \xC2");
	snprintf(text, sizeof(text),
		 "To: \"Cohen, Dana\" <dana@example.com>, avi@example.org\n"
		 "Cc: \n"
		 "Bcc: secret@example.net\n"
		 "Subject: %s 1990\n"
		 "\n"
		 "%s, this is MAIL.\n.leading dot\n", shalom_atari, shalom_atari);
	raw = compose_build(&a, text, strlen(text), "<abc@example.com>", "<abc@example.com>", err, sizeof(err), &n);
	CHECK(raw != 0, "build: %s", err);
	if (!raw)
		return;
	k = compose_rcpts(raw, n, rc, 10);
	CHECK(k == 3 && !strcmp(rc[0], "dana@example.com") && !strcmp(rc[2], "secret@example.net"),
	      "rcpts %d", k);
	smtp = compose_for_smtp(raw, n, &sl);
	CHECK(!strstr(smtp, "secret@"), "bcc stripped");
	CHECK(strstr(smtp, "Content-Transfer-Encoding: quoted-printable") != 0, "qp body");
	m = mime_parse(smtp, sl);
	CHECK(!strcmp(m->subject, "\xD6\xCD\xC7\xDA 1990"), "subject round trip [%s]", m->subject);
	CHECK(strstr(m->text, "\xD6\xCD\xC7\xDA, this is MAIL.") != 0, "body round trip [%s]", m->text);
	CHECK(strstr(m->from, "Yaary \xC2") != 0, "from name round trip [%s]", m->from);
	CHECK(strstr(m->to, "Cohen, Dana") != 0, "quoted name [%s]", m->to);
	mime_free(m);
	free(raw);
	free(smtp);

	strcpy(a.signature, "Yaary\nSent from my Falcon");
	raw = compose_new(&a, "dana@example.com");
	CHECK(!strcmp(raw, "To: dana@example.com\nCc: \nSubject: \n\n\n\n-- \nYaary\nSent from my Falcon"),
	      "new message with signature [%s]", raw);
	free(raw);
}

static void t_bidi(void)
{
	char out[64];
	/* "shalom 1990" in a Hebrew paragraph: number stays LTR, word reversed, at the right */
	const char *s = "\xD6\xCD\xC7\xDA 1990";
	bidi_visual(s, (short)strlen(s), bidi_is_rtl(s, (short)strlen(s)), out);
	CHECK(!memcmp(out, "1990 \xDA\xC7\xCD\xD6", 9), "bidi visual [%.9s]", out);
}

/* the root certificates shipped with MAIL load as BearSSL trust anchors */
static void t_tls(void)
{
	char err[200];
	int n;
	snprintf(tls_roots_cache, sizeof(tls_roots_cache), "/tmp/mail-unit-roots.%d", (int)getpid());
	remove(tls_roots_cache);
	n = tls_load_anchors("CACERT.PEM", err, sizeof(err));
	CHECK(n == 121, "CACERT.PEM: %d roots (%s)", n, n ? "" : err);
	CHECK(tls_load_anchors("CACERT.PEM", err, sizeof(err)) == n, "anchors loaded once");
	CHECK(pf_size(tls_roots_cache) > 1000, "decoded roots cached");
	tls_free_anchors();
	n = tls_load_anchors("CACERT.PEM", err, sizeof(err));
	CHECK(n == 121, "roots read back from the cache: %d", n);
	remove(tls_roots_cache);
}

static void t_num(void)
{
	CHECK(!strcmp(num(0), "0"), "0");
	CHECK(!strcmp(num(999), "999"), "999");
	CHECK(!strcmp(num(1000), "1,000"), "1,000: %s", num(1000));
	CHECK(!strcmp(num(1234567), "1,234,567"), "1,234,567: %s", num(1234567));
	CHECK(!strcmp(num(-45210), "-45,210"), "-45,210: %s", num(-45210));
}

int main(void)
{
	t_num();
	t_charset();
	t_mime();
	t_compose();
	t_bidi();
	t_tls();
	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
