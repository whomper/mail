/*
 * store.c - settings, accounts, folder lists, header indexes and the
 * message cache (see store.h for the layout on disk).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "plat.h"
#include "store.h"
#include "charset.h"
#include "mime.h"
#include "util.h"
#include "conn.h"
#include "tls.h"

OPTIONS opt;
ACCOUNT *accts[MAXACCT];
short naccts;

static char inf_path[220], mail_dir[220];

/* ---------------- settings ---------------- */

static void defaults(void)
{
	opt.tz = 0;
	opt.check = 0;
	opt.page = 100;
	opt.keepcache = 1;
	opt.log = 0;
	opt.hebrew = 0;
	opt.offline = 0;
	opt.wrap = 72;
	opt.font_id = 1;
	opt.font_pt = 0;	/* the system font at the screen's size */
}

/* "a,b,c,d" -> up to n shorts */
static void shorts(const char *v, short *out, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		char *e;
		out[i] = (short)strtol(v, &e, 10);
		if (*e != ',')
			break;
		v = e + 1;
	}
}

/* \n and \\ escapes keep multi-line values on one line */
static void unescape(char *d, const char *s, int size)
{
	int o = 0;
	while (*s && o < size - 1) {
		if (*s == '\\' && s[1] == 'n') {
			d[o++] = '\n';
			s += 2;
		} else if (*s == '\\' && s[1] == '\\') {
			d[o++] = '\\';
			s += 2;
		} else {
			d[o++] = *s++;
		}
	}
	d[o] = 0;
}

static void escape(SBUF *b, const char *s)
{
	for (; *s; s++) {
		if (*s == '\n')
			sb_adds(b, "\\n");
		else if (*s == '\\')
			sb_adds(b, "\\\\");
		else if (*s != '\r')
			sb_addc(b, *s);
	}
}

ACCOUNT *acct_new(void)
{
	ACCOUNT *a;
	short id, i;
	if (naccts >= MAXACCT)
		return 0;
	a = calloc(1, sizeof(ACCOUNT));
	if (!a)
		return 0;
	/* first free id */
	for (id = 1; id <= MAXACCT; id++) {
		for (i = 0; i < naccts; i++)
			if (accts[i]->id == id)
				break;
		if (i == naccts)
			break;
	}
	a->id = id;
	a->port = 143;
	a->smtpport = 587;
	a->leave = 1;
	strcpy(a->name, "Mail");
	accts[naccts++] = a;
	acct_dirs(a);
	return a;
}

const char *acct_host(ACCOUNT *a, int smtp)
{
	if (opt.falcon) {
		if (smtp && a->dsmtphost[0])
			return a->dsmtphost;
		return a->dhost;
	}
	if (smtp && a->smtphost[0])
		return a->smtphost;
	return a->host;
}

unsigned short acct_port(ACCOUNT *a, int smtp)
{
	if (opt.falcon)
		return smtp ? (a->dsmtpport ? a->dsmtpport : 465) : (a->dport ? a->dport : (a->pop ? 995 : 993));
	return smtp ? a->smtpport : a->port;
}

int acct_sec(ACCOUNT *a, int smtp)
{
	unsigned short p;
	short s;
	if (!opt.falcon)
		return SEC_PLAIN;
	s = smtp ? a->dsmtpsec : a->dsec;
	if (s)
		return s;
	p = acct_port(a, smtp);
	return p == 993 || p == 995 || p == 465 ? SEC_TLS : SEC_STARTTLS;
}

/* the big providers' servers, for Falcon mode */
static const struct {
	const char *domains, *in, *out;
	unsigned short inport, outport;
} presets[] = {
	{ " gmail.com googlemail.com ", "imap.gmail.com", "smtp.gmail.com", 993, 465 },
	{ " icloud.com me.com mac.com ", "imap.mail.me.com", "smtp.mail.me.com", 993, 587 },
	{ " yahoo.com ymail.com ", "imap.mail.yahoo.com", "smtp.mail.yahoo.com", 993, 465 },
	{ " gmx.net gmx.de gmx.com ", "imap.gmx.net", "mail.gmx.net", 993, 465 },
	{ " web.de ", "imap.web.de", "smtp.web.de", 993, 587 },
	{ " aol.com ", "imap.aol.com", "smtp.aol.com", 993, 465 },
	{ " fastmail.com fastmail.fm ", "imap.fastmail.com", "smtp.fastmail.com", 993, 465 },
	{ " zoho.com ", "imap.zoho.com", "smtp.zoho.com", 993, 465 },
};

int acct_preset(ACCOUNT *a)
{
	const char *at = strrchr(a->email, '@');
	char key[80];
	unsigned i;
	if (!at)
		return 0;
	snprintf(key, sizeof(key), " %s ", at + 1);
	for (i = 0; key[i]; i++)
		key[i] = (char)tolower((unsigned char)key[i]);
	for (i = 0; i < sizeof(presets) / sizeof(presets[0]); i++) {
		if (!strstr(presets[i].domains, key))
			continue;
		str_copy(a->dhost, presets[i].in, sizeof(a->dhost));
		a->dport = presets[i].inport;
		str_copy(a->dsmtphost, presets[i].out, sizeof(a->dsmtphost));
		a->dsmtpport = presets[i].outport;
		a->dsec = a->dsmtpsec = 0;
		return 1;
	}
	return 0;
}

void acct_dirs(ACCOUNT *a)
{
	char name[16];
	snprintf(name, sizeof(name), "ACCT%d", a->id);
	path_join(a->dir, sizeof(a->dir), mail_dir, name);
	pf_mkdir(a->dir);
}

void acct_delete(ACCOUNT *a)
{
	short i, j;
	for (i = 0; i < naccts; i++) {
		if (accts[i] != a)
			continue;
		for (j = i; j < naccts - 1; j++)
			accts[j] = accts[j + 1];
		naccts--;
		break;
	}
	if (a->im)
		imap_logout(a->im);
	free(a);
}

static void set_acct(ACCOUNT *a, const char *k, const char *v)
{
	char tmp[256];
	unescape(tmp, v, sizeof(tmp));
	if (!strcmp(k, "name")) str_copy(a->name, tmp, sizeof(a->name));
	else if (!strcmp(k, "fullname")) str_copy(a->fullname, tmp, sizeof(a->fullname));
	else if (!strcmp(k, "email")) str_copy(a->email, tmp, sizeof(a->email));
	else if (!strcmp(k, "in")) a->pop = !strcasecmp(tmp, "pop3");
	else if (!strcmp(k, "host")) str_copy(a->host, tmp, sizeof(a->host));
	else if (!strcmp(k, "port")) a->port = (unsigned short)atoi(tmp);
	else if (!strcmp(k, "user")) str_copy(a->user, tmp, sizeof(a->user));
	else if (!strcmp(k, "pass")) str_copy(a->pass, tmp, sizeof(a->pass));
	else if (!strcmp(k, "leave")) a->leave = (short)atoi(tmp);
	else if (!strcmp(k, "smtphost")) str_copy(a->smtphost, tmp, sizeof(a->smtphost));
	else if (!strcmp(k, "smtpport")) a->smtpport = (unsigned short)atoi(tmp);
	else if (!strcmp(k, "smtpuser")) str_copy(a->smtpuser, tmp, sizeof(a->smtpuser));
	else if (!strcmp(k, "smtppass")) str_copy(a->smtppass, tmp, sizeof(a->smtppass));
	else if (!strcmp(k, "sent")) str_copy(a->sentname, tmp, sizeof(a->sentname));
	else if (!strcmp(k, "signature")) str_copy(a->signature, tmp, sizeof(a->signature));
	else if (!strcmp(k, "dhost")) str_copy(a->dhost, tmp, sizeof(a->dhost));
	else if (!strcmp(k, "dport")) a->dport = (unsigned short)atoi(tmp);
	else if (!strcmp(k, "dsmtphost")) str_copy(a->dsmtphost, tmp, sizeof(a->dsmtphost));
	else if (!strcmp(k, "dsmtpport")) a->dsmtpport = (unsigned short)atoi(tmp);
	else if (!strcmp(k, "dsec")) a->dsec = (short)atoi(tmp);
	else if (!strcmp(k, "dsmtpsec")) a->dsmtpsec = (short)atoi(tmp);
}

static void set_opt(const char *k, const char *v)
{
	short n = (short)atoi(v);
	if (!strcmp(k, "tz")) opt.tz = n;
	else if (!strcmp(k, "check")) opt.check = n;
	else if (!strcmp(k, "page")) opt.page = n < 20 ? 20 : n > 1000 ? 1000 : n;
	else if (!strcmp(k, "keepcache")) opt.keepcache = n;
	else if (!strcmp(k, "log")) opt.log = n;
	else if (!strcmp(k, "hebrew")) opt.hebrew = n;
	else if (!strcmp(k, "offline")) opt.offline = n;
	else if (!strcmp(k, "wrap")) opt.wrap = n < 40 ? 40 : n > 78 ? 78 : n;
	else if (!strcmp(k, "main")) shorts(v, &opt.main_x, 4);
	else if (!strcmp(k, "panes")) shorts(v, &opt.pane_w, 2);
	else if (!strcmp(k, "editor")) shorts(v, &opt.ed_x, 4);
	else if (!strcmp(k, "font")) shorts(v, &opt.font_id, 2);
	else if (!strcmp(k, "falcon")) opt.falcon = n != 0;
	else if (!strcmp(k, "bridgeorder")) cs_bridge_visual = n != 0;
	else if (!strcmp(k, "hebfont")) opt.hebfont = n >= 0 && n <= 2 ? n : 0;
}

int store_init(const char *workdir)
{
	char *buf, *line, *next;
	ACCOUNT *cur = 0;
	short in_opts = 0;

	defaults();
	str_copy(opt.workdir, workdir, sizeof(opt.workdir));
	path_join(inf_path, sizeof(inf_path), workdir, "MAIL.INF");
	path_join(mail_dir, sizeof(mail_dir), workdir, "MAIL");
	pf_mkdir(mail_dir);
	/* Falcon mode: the root certificates next to MAIL.PRG, the random
	   seed with the mail */
	path_join(conn_cacert, sizeof(conn_cacert), workdir, "CACERT.PEM");
	path_join(tls_seed_path, sizeof(tls_seed_path), mail_dir, "SEED.DAT");

	buf = pf_load(inf_path, 0);
	if (!buf)
		return 0;
	for (line = buf; line && *line; line = next) {
		char *eq, *k;
		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		k = str_trim(line);
		if (*k == ';' || *k == '#' || !*k)
			continue;
		if (*k == '[') {
			in_opts = !strncasecmp(k, "[options]", 9);
			cur = !strncasecmp(k, "[account]", 9) ? acct_new() : 0;
			continue;
		}
		eq = strchr(k, '=');
		if (!eq)
			continue;
		*eq = 0;
		{
			char *key = str_trim(k), *val = eq + 1;
			char *sc;
			long i;
			/* "value ; comment" - but passwords may contain ';' */
			if (strcmp(key, "pass") && strcmp(key, "smtppass") && strcmp(key, "signature") &&
			    (sc = strstr(val, " ;")))
				*sc = 0;
			val = str_trim(val);
			for (i = 0; key[i]; i++)
				key[i] = (char)tolower((unsigned char)key[i]);
			if (cur)
				set_acct(cur, key, val);
			else if (in_opts)
				set_opt(key, val);
		}
	}
	free(buf);
	tls_tz_minutes = opt.tz;
	return naccts;
}

/* MAIL.INF explains itself: a ';' line above every setting. MAIL skips
 * those lines when reading and writes them anew on every save. */
static void note(SBUF *b, const char *s)
{
	sb_adds(b, "; ");
	sb_adds(b, s);
	sb_adds(b, "\r\n");
}

int store_save_settings(void)
{
	SBUF b;
	short i;
	int r;
	tls_tz_minutes = opt.tz;
	sb_init(&b);
	sb_adds(&b, "; MAIL settings, written by MAIL.PRG. Lines starting with ; are notes.\r\n"
		    "; Most of these are set in Options > Settings; see docs/GUIDE.md.\r\n"
		    "; 1 means on, 0 means off.\r\n\r\n[options]\r\n");
	note(&b, "your time zone, in minutes east of UTC (Israel: 120 winter, 180 summer)");
	sb_printf(&b, "tz=%d\r\n", opt.tz);
	note(&b, "check for new mail every this many minutes; 0 = only when you ask");
	sb_printf(&b, "check=%d\r\n", opt.check);
	note(&b, "messages loaded at a time in a folder; \"Load more\" gets the next ones (20-1000)");
	sb_printf(&b, "page=%d\r\n", opt.page);
	note(&b, "1 = keep messages you have read on disk after quitting, 0 = only their headers");
	sb_printf(&b, "keepcache=%d\r\n", opt.keepcache);
	note(&b, "1 = write the conversation with the servers to MAIL.LOG (passwords hidden)");
	sb_printf(&b, "log=%d\r\n", opt.log);
	note(&b, "1 = start with the Hebrew keyboard (F10 switches)");
	sb_printf(&b, "hebrew=%d\r\n", opt.hebrew);
	note(&b, "1 = work offline: don't connect, keep new messages in the Outbox");
	sb_printf(&b, "offline=%d\r\n", opt.offline);
	note(&b, "the editor wraps lines at this column (40-78)");
	sb_printf(&b, "wrap=%d\r\n", opt.wrap);
	note(&b, "main window: x,y,width,height in pixels; 0,0,0,0 = let MAIL place it");
	sb_printf(&b, "main=%d,%d,%d,%d\r\n", opt.main_x, opt.main_y, opt.main_w, opt.main_h);
	note(&b, "dividers: folder pane width, message list height, in pixels");
	sb_printf(&b, "panes=%d,%d\r\n", opt.pane_w, opt.pane_h);
	note(&b, "editor window: x,y,width,height in pixels; 0,0,0,0 = let MAIL place it");
	sb_printf(&b, "editor=%d,%d,%d,%d\r\n", opt.ed_x, opt.ed_y, opt.ed_w, opt.ed_h);
	note(&b, "text font: GDOS font id (1 = system font), size in points");
	note(&b, "(system font: 0 = normal, 8 = small, 16 = large); set with Options > Font");
	sb_printf(&b, "font=%d,%d\r\n", opt.font_id, opt.font_pt);
	note(&b, "where the screen font has its Hebrew letters: 0 = Atari (standard TOS font),");
	note(&b, "1 = ISO-8859-8 places (Israeli Hebrew fonts), 2 = DOS 862 places; set in Options > Font");
	sb_printf(&b, "hebfont=%d\r\n", opt.hebfont);
	note(&b, "1 = Falcon mode: MAIL connects to the mail providers itself, with TLS (the");
	note(&b, "DSP checks signatures); 0 = through the Raspberry Pi gateway. Options > Settings");
	sb_printf(&b, "falcon=%d\r\n", opt.falcon);
	note(&b, "1 = Hebrew that arrives already in Atari characters (from a bridge made for");
	note(&b, "Troll) is in display order: turn it back into reading order; set in Options > Settings");
	sb_printf(&b, "bridgeorder=%d\r\n", cs_bridge_visual);
	for (i = 0; i < naccts; i++) {
		ACCOUNT *a = accts[i];
		sb_adds(&b, "\r\n; one [account] part per account, up to 8; Options > Accounts edits them\r\n"
			    "[account]\r\n");
		note(&b, "the account's name in the folder list");
		sb_adds(&b, "name=");
		escape(&b, a->name);
		sb_adds(&b, "\r\n");
		note(&b, "your name, as people you write to see it");
		sb_adds(&b, "fullname=");
		escape(&b, a->fullname);
		sb_adds(&b, "\r\n");
		note(&b, "your e-mail address");
		sb_printf(&b, "email=%s\r\n", a->email);
		note(&b, "incoming mail: imap (folders stay on the server) or pop3 (mail comes to the Atari)");
		sb_printf(&b, "in=%s\r\n", a->pop ? "pop3" : "imap");
		note(&b, "incoming server and port; through the Pi gateway: its IP, 143 (IMAP) or 110 (POP3)");
		sb_printf(&b, "host=%s\r\nport=%u\r\n", a->host, a->port);
		note(&b, "login for the incoming server");
		sb_adds(&b, "user=");
		escape(&b, a->user);
		sb_adds(&b, "\r\n");
		note(&b, "password, as typed: keep this file to yourself");
		sb_adds(&b, "pass=");
		escape(&b, a->pass);
		sb_adds(&b, "\r\n");
		note(&b, "POP3 only: 1 = leave mail on the server after downloading it");
		sb_printf(&b, "leave=%d\r\n", a->leave);
		note(&b, "outgoing (SMTP) server and port; through the Pi gateway: its IP and 587");
		sb_printf(&b, "smtphost=%s\r\nsmtpport=%u\r\n", a->smtphost, a->smtpport);
		note(&b, "SMTP login: empty = same as incoming, - = the server needs no login");
		sb_adds(&b, "smtpuser=");
		escape(&b, a->smtpuser);
		sb_adds(&b, "\r\n");
		note(&b, "SMTP password, if the SMTP login differs");
		sb_adds(&b, "smtppass=");
		escape(&b, a->smtppass);
		sb_adds(&b, "\r\n");
		note(&b, "IMAP folder for copies of sent mail; empty = the server's Sent folder");
		sb_adds(&b, "sent=");
		escape(&b, a->sentname);
		sb_adds(&b, "\r\n");
		note(&b, "Falcon mode: the provider's own servers and ports; 993/995/465 are TLS,");
		note(&b, "other ports STARTTLS (dsec/dsmtpsec 1 = TLS, 2 = STARTTLS overrides)");
		sb_adds(&b, "dhost=");
		escape(&b, a->dhost);
		sb_printf(&b, "\r\ndport=%u\r\ndsmtphost=", a->dport);
		escape(&b, a->dsmtphost);
		sb_printf(&b, "\r\ndsmtpport=%u\r\n", a->dsmtpport);
		if (a->dsec || a->dsmtpsec)
			sb_printf(&b, "dsec=%d\r\ndsmtpsec=%d\r\n", a->dsec, a->dsmtpsec);
		note(&b, "added below new messages; \\n starts a new line (| in the account dialog)");
		sb_adds(&b, "signature=");
		escape(&b, a->signature);
		sb_adds(&b, "\r\n");
	}
	r = pf_save(inf_path, b.s, b.len);
	sb_free(&b);
	return r;
}

/* ---------------- folder lists ---------------- */

static void folder_names(FINFO *f, char delim)
{
	const char *last = f->server, *p;
	char *d;
	f->depth = 0;
	if (delim) {
		for (p = f->server; *p; p++)
			if (*p == delim && p[1]) {
				f->depth++;
				last = p + 1;
			}
	}
	if (f->role == FR_INBOX && !f->local) {
		strcpy(f->disp, "Inbox");
		return;
	}
	d = mutf7_to_atari(last);
	str_copy(f->disp, d ? d : last, sizeof(f->disp));
	free(d);
}

FINFO *folder_add(ACCOUNT *a, const char *server, char delim, short role, short noselect, short local)
{
	FINFO *f = folder_find(a, server);
	char path[220];
	if (!f) {
		if (a->nfolders >= MAXFOLDER)
			return 0;
		f = &a->folders[a->nfolders++];
		memset(f, 0, sizeof(*f));
		str_copy(f->server, server, sizeof(f->server));
		snprintf(f->dir, sizeof(f->dir), "F%07lX", (str_hash(server) + (local ? 1 : 0)) & 0xFFFFFFFL);
	}
	f->role = role;
	f->noselect = noselect;
	f->local = local;
	folder_names(f, delim);
	if (!noselect) {
		path_join(path, sizeof(path), a->dir, f->dir);
		pf_mkdir(path);
	}
	return f;
}

FINFO *folder_find(ACCOUNT *a, const char *server)
{
	short i;
	for (i = 0; i < a->nfolders; i++)
		if (!strcmp(a->folders[i].server, server))
			return &a->folders[i];
	return 0;
}

FINFO *folder_role(ACCOUNT *a, short role)
{
	short i;
	for (i = 0; i < a->nfolders; i++)
		if (a->folders[i].role == role)
			return &a->folders[i];
	/* well-known names when the server has no SPECIAL-USE */
	if (role == FR_SENT || role == FR_TRASH || role == FR_DRAFTS || role == FR_JUNK) {
		static const char *names[][4] = {
			{ "Sent", "Sent Items", "Sent Messages", "INBOX.Sent" },
			{ "Trash", "Deleted Items", "Deleted Messages", "INBOX.Trash" },
			{ "Drafts", "INBOX.Drafts", "Draft", "Drafts" },
			{ "Junk", "Spam", "INBOX.Junk", "INBOX.Spam" },
		};
		short r = role == FR_SENT ? 0 : role == FR_TRASH ? 1 : role == FR_DRAFTS ? 2 : 3, k;
		if (role == FR_SENT && a->sentname[0])
			return folder_find(a, a->sentname);
		for (k = 0; k < 4; k++)
			for (i = 0; i < a->nfolders; i++)
				if (!a->folders[i].local && !strcasecmp(a->folders[i].server, names[r][k]))
					return &a->folders[i];
	}
	return 0;
}

void folder_path(ACCOUNT *a, FINFO *f, char *out, int size)
{
	path_join(out, size, a->dir, f->dir);
}

int folders_save(ACCOUNT *a)
{
	SBUF b;
	char path[220];
	short i;
	int r;
	sb_init(&b);
	for (i = 0; i < a->nfolders; i++) {
		FINFO *f = &a->folders[i];
		sb_printf(&b, "%d\t%d\t%d\t%ld\t%ld\t%s\r\n", f->role, f->noselect, f->local,
			  f->total, f->unread, f->server);
	}
	path_join(path, sizeof(path), a->dir, "FOLDERS.LST");
	r = pf_save(path, b.s ? b.s : "", b.len);
	sb_free(&b);
	return r;
}

/* the local folders every account has */
static void local_folders(ACCOUNT *a)
{
	folder_add(a, "Outbox", 0, FR_OUTBOX, 0, 1);
	if (a->pop) {
		folder_add(a, "Inbox", 0, FR_INBOX, 0, 1);
		folder_add(a, "Sent", 0, FR_SENT, 0, 1);
		folder_add(a, "Trash", 0, FR_TRASH, 0, 1);
	}
}

int folders_load(ACCOUNT *a)
{
	char path[220], *buf, *line, *next;
	a->nfolders = 0;
	path_join(path, sizeof(path), a->dir, "FOLDERS.LST");
	buf = pf_load(path, 0);
	if (buf) {
		for (line = buf; line && *line; line = next) {
			char *f[6];
			short n = 0;
			next = strchr(line, '\n');
			if (next)
				*next++ = 0;
			if (*line && line[strlen(line) - 1] == '\r')
				line[strlen(line) - 1] = 0;
			f[n++] = line;
			while (n < 6) {
				char *t = strchr(f[n - 1], '\t');
				if (!t)
					break;
				*t = 0;
				f[n++] = t + 1;
			}
			if (n == 6 && *f[5]) {
				FINFO *fi = folder_add(a, f[5], '/', (short)atoi(f[0]), (short)atoi(f[1]), (short)atoi(f[2]));
				if (fi) {
					fi->total = atol(f[3]);
					fi->unread = atol(f[4]);
				}
			}
		}
		free(buf);
	}
	local_folders(a);
	return a->nfolders;
}

/* ---------------- header index ---------------- */

static char *dupclean(const char *s)
{
	char *d = strdup(s ? s : ""), *p;
	if (!d)
		return 0;
	for (p = d; *p; p++)
		if (*p == '\t' || *p == '\r' || *p == '\n')
			*p = ' ';
	return d;
}

void hdr_set(HDR *h, const char *from, const char *subject, const char *to)
{
	free(h->from);
	free(h->subject);
	free(h->to);
	h->from = dupclean(from);
	h->subject = dupclean(subject);
	h->to = dupclean(to);
}

static long find(FOLDER *f, unsigned long uid, int *found)
{
	long lo = 0, hi = f->n - 1;
	while (lo <= hi) {
		long mid = (lo + hi) / 2;
		if (f->h[mid].uid == uid) {
			*found = 1;
			return mid;
		}
		if (f->h[mid].uid < uid)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	*found = 0;
	return lo;
}

HDR *fold_get(FOLDER *f, unsigned long uid)
{
	int found;
	long i = find(f, uid, &found);
	return found ? &f->h[i] : 0;
}

HDR *fold_add(FOLDER *f, unsigned long uid)
{
	int found;
	long i = find(f, uid, &found);
	if (found)
		return &f->h[i];
	if (f->n == f->cap) {
		long cap = f->cap ? f->cap * 2 : 64;
		HDR *n = realloc(f->h, cap * sizeof(HDR));
		if (!n)
			return 0;
		f->h = n;
		f->cap = cap;
	}
	memmove(f->h + i + 1, f->h + i, (f->n - i) * sizeof(HDR));
	f->n++;
	memset(&f->h[i], 0, sizeof(HDR));
	f->h[i].uid = uid;
	f->dirty = 1;
	return &f->h[i];
}

void msg_path(FOLDER *f, unsigned long uid, char *out, int size)
{
	char name[16];
	snprintf(name, sizeof(name), "%08lX.EML", uid);
	path_join(out, size, f->dir, name);
}

void fold_remove(FOLDER *f, unsigned long uid)
{
	int found;
	long i = find(f, uid, &found);
	char path[220];
	if (!found)
		return;
	msg_path(f, uid, path, sizeof(path));
	pf_remove(path);
	free(f->h[i].from);
	free(f->h[i].subject);
	free(f->h[i].to);
	memmove(f->h + i, f->h + i + 1, (f->n - i - 1) * sizeof(HDR));
	f->n--;
	f->dirty = 1;
}

void fold_clear(FOLDER *f)
{
	while (f->n)
		fold_remove(f, f->h[f->n - 1].uid);
}

void fold_count(FOLDER *f)
{
	long i, u = 0;
	/* IMAP folders show the server's counts (STATUS), kept up to date by
	   the operations in mail.c; only a part of them is on disk */
	if (f->fi && !f->fi->local)
		return;
	for (i = 0; i < f->n; i++)
		if (!(f->h[i].flags & (MF_SEEN | MF_DELETED)))
			u++;
	if (f->fi) {
		f->fi->total = f->n;
		f->fi->unread = u;
	}
}

/* Index files before version 3 hold headers that MAIL 0.2 misread when
 * a bridge had already turned them into Atari text. Read them again from
 * the messages on disk; IMAP headers without one are dropped, and the
 * next sync fetches them again from the server. */
static void upgrade_headers(FOLDER *f)
{
	long i;
	for (i = f->n - 1; i >= 0; i--) {
		HDR *h = &f->h[i];
		char path[220], *raw;
		long len;
		msg_path(f, h->uid, path, sizeof(path));
		raw = pf_load(path, &len);
		if (raw) {
			unsigned short flags = h->flags;
			long size = h->size;
			hdr_from_raw(h, raw, len);
			h->flags |= flags;
			h->size = size;
			free(raw);
		} else if (f->fi && !f->fi->local) {
			fold_remove(f, h->uid);
		}
	}
	f->dirty = 1;
}

FOLDER *fold_open(ACCOUNT *a, FINFO *fi)
{
	FOLDER *f = calloc(1, sizeof(FOLDER));
	char path[220], *buf, *line, *next;
	short version;
	if (!f)
		return 0;
	f->acct = a;
	f->fi = fi;
	folder_path(a, fi, f->dir, sizeof(f->dir));
	pf_mkdir(f->dir);
	f->uidnext = 1;
	path_join(path, sizeof(path), f->dir, "INDEX.DAT");
	buf = pf_load(path, 0);
	if (!buf)
		return f;
	version = 0;
	for (line = buf; line && *line; line = next) {
		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		if (!strncmp(line, "MAILIDX ", 8)) {
			char *e;
			version = (short)strtoul(line + 8, &e, 10);
			f->uidvalidity = strtoul(e, &e, 10);
			f->uidnext = strtoul(e, &e, 10);
			f->window = strtol(e, &e, 10);
			f->exists = strtol(e, &e, 10);
			continue;
		}
		{
			char *fl[7];
			short n = 1;
			HDR *h;
			fl[0] = line;
			while (n < 7 && (fl[n] = strchr(fl[n - 1], '\t'))) {
				*fl[n] = 0;
				fl[n]++;
				n++;
			}
			if (n < 7)
				continue;
			if (fl[6][0] && fl[6][strlen(fl[6]) - 1] == '\r')
				fl[6][strlen(fl[6]) - 1] = 0;
			h = fold_add(f, strtoul(fl[0], 0, 10));
			if (!h)
				break;
			h->flags = (unsigned short)strtoul(fl[1], 0, 16);
			h->size = atol(fl[2]);
			h->date = strtoul(fl[3], 0, 10);
			hdr_set(h, fl[4], fl[5], fl[6]);
		}
	}
	free(buf);
	f->dirty = 0;
	if (version < 3)
		upgrade_headers(f);
	fold_count(f);
	return f;
}

int fold_save(FOLDER *f)
{
	SBUF b;
	char path[220];
	long i;
	int r;
	sb_init(&b);
	sb_printf(&b, "MAILIDX 3 %lu %lu %ld %ld\n", f->uidvalidity, f->uidnext, f->window, f->exists);
	for (i = 0; i < f->n; i++) {
		HDR *h = &f->h[i];
		sb_printf(&b, "%lu\t%x\t%ld\t%lu\t", h->uid, h->flags, h->size, h->date);
		sb_adds(&b, h->from ? h->from : "");
		sb_addc(&b, '\t');
		sb_adds(&b, h->subject ? h->subject : "");
		sb_addc(&b, '\t');
		sb_adds(&b, h->to ? h->to : "");
		sb_addc(&b, '\n');
	}
	path_join(path, sizeof(path), f->dir, "INDEX.DAT");
	r = pf_save(path, b.s, b.len);
	sb_free(&b);
	f->dirty = 0;
	fold_count(f);
	return r;
}

void fold_close(FOLDER *f)
{
	long i;
	if (!f)
		return;
	if (f->dirty)
		fold_save(f);
	for (i = 0; i < f->n; i++) {
		free(f->h[i].from);
		free(f->h[i].subject);
		free(f->h[i].to);
	}
	free(f->h);
	free(f);
}

void hdr_from_raw(HDR *h, const char *raw, long len)
{
	long hl = mime_header_len(raw, len);
	char *from = mime_header(raw, hl, "From"), *subj = mime_header(raw, hl, "Subject");
	char *to = mime_header(raw, hl, "To"), *date = mime_header(raw, hl, "Date");
	char *ct = mime_header(raw, hl, "Content-Type");
	char *df = hdr_decode(from ? from : ""), *ds = hdr_decode(subj ? subj : ""),
	     *dt = hdr_decode(to ? to : "");
	hdr_set(h, df, ds, dt);
	h->date = mime_date(date);
	if (ct && str_istr(ct, "multipart/mixed"))
		h->flags |= MF_ATTACH;
	free(from);
	free(subj);
	free(to);
	free(date);
	free(ct);
	free(df);
	free(ds);
	free(dt);
}

int msg_save(FOLDER *f, unsigned long uid, const char *data, long len)
{
	char path[220];
	msg_path(f, uid, path, sizeof(path));
	return pf_save(path, data, len);
}

char *msg_load(FOLDER *f, unsigned long uid, long *len)
{
	char path[220];
	msg_path(f, uid, path, sizeof(path));
	return pf_load(path, len);
}

void cache_clear_all(void)
{
	short i, j;
	for (i = 0; i < naccts; i++) {
		ACCOUNT *a = accts[i];
		for (j = 0; j < a->nfolders; j++) {
			FINFO *fi = &a->folders[j];
			FOLDER *f;
			long k;
			if (fi->local || fi->noselect)
				continue;
			f = fold_open(a, fi);
			if (!f)
				continue;
			for (k = 0; k < f->n; k++) {
				if (f->h[k].flags & MF_CACHED) {
					char path[220];
					msg_path(f, f->h[k].uid, path, sizeof(path));
					pf_remove(path);
					f->h[k].flags &= ~MF_CACHED;
					f->dirty = 1;
				}
			}
			fold_close(f);
		}
	}
}

/* ---------------- address book ---------------- */

char *abook_load(long *len)
{
	char path[220];
	path_join(path, sizeof(path), opt.workdir, "ADDRESS.TXT");
	return pf_load(path, len);
}

int abook_add(const char *addr)
{
	char path[220], email[96], *buf;
	int h;
	addr_split(addr, 0, 0, email, sizeof(email));
	if (!*email || !strchr(email, '@'))
		return 0;
	buf = abook_load(0);
	if (buf && str_istr(buf, email)) {
		free(buf);
		return 0;
	}
	free(buf);
	path_join(path, sizeof(path), opt.workdir, "ADDRESS.TXT");
	h = pf_open(path, PF_APPEND);
	if (h < 0)
		return -1;
	pf_write(h, addr, strlen(addr));
	pf_write(h, "\r\n", 2);
	pf_close(h);
	return 1;
}

/* ---------------- dates ---------------- */

void date_str(unsigned long t, char *out, int size, int with_year)
{
	long z, era, doe, yoe, y, doy, mp, d, m;
	unsigned long secs;
	if (!t) {
		str_copy(out, "", size);
		return;
	}
	t += (long)opt.tz * 60;
	secs = t % 86400;
	z = t / 86400 + 719468;
	era = z / 146097;
	doe = z - era * 146097;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	y = yoe + era * 400;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	d = doy - (153 * mp + 2) / 5 + 1;
	m = mp < 10 ? mp + 3 : mp - 9;
	if (m <= 2)
		y++;
	if (with_year)
		snprintf(out, size, "%02ld.%02ld.%02ld %02lu:%02lu", d, m, y % 100, secs / 3600, (secs / 60) % 60);
	else
		snprintf(out, size, "%02ld.%02ld %02lu:%02lu", d, m, secs / 3600, (secs / 60) % 60);
}
