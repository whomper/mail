/*
 * mail.c - mail operations on top of the protocol clients and the store.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "plat.h"
#include "mail.h"
#include "imap.h"
#include "pop3.h"
#include "smtp.h"
#include "mime.h"
#include "compose.h"
#include "charset.h"
#include "util.h"
#include "conn.h"

char mail_err[200];
int mail_unreachable;
void (*mail_status)(const char *msg);

static void status(const char *fmt, ...)
{
	char tmp[160];
	va_list ap;
	if (!mail_status)
		return;
	va_start(ap, fmt);
	vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	mail_status(tmp);
}

/* ---------------- progress on the status line ---------------- */

/* "Home: downloading the message  [######....] 60%  29 KB": by bytes
   against an expected size, by items (headers) against a count, or just
   the bytes so far when the size isn't known. Shown at most four times a
   second: the info line is redrawn by the AES. */
static struct {
	char label[120];
	int out;			/* counting bytes sent, not received */
	long base, total;		/* bytes */
	long items, nitems;
	unsigned long last;
	int on;
} prog;

static long prog_bytes(void)
{
	return (prog.out ? conn_bytes_out : conn_bytes) - prog.base;
}

static void prog_show(void)
{
	char bar[16], line[200];
	long done = prog_bytes(), pct = -1;
	short i;
	prog.last = pf_ms();
	if (prog.nitems > 0)
		pct = prog.items * 100 / prog.nitems;
	else if (prog.total > 0)
		pct = done / (prog.total / 100 + 1);
	if (pct > 100)
		pct = 100;
	if (pct < 0) {
		snprintf(line, sizeof(line), "%s  %s KB", prog.label, num((done + 512) / 1024));
	} else {
		for (i = 0; i < 10; i++)
			bar[i] = i < pct / 10 ? '#' : '.';
		bar[10] = 0;
		if (prog.nitems > 0)
			snprintf(line, sizeof(line), "%s %s of %s  [%s] %ld%%", prog.label,
				 num(prog.items), num(prog.nitems), bar, pct);
		else
			snprintf(line, sizeof(line), "%s  [%s] %ld%%  %s KB", prog.label, bar, pct,
				 num((done + 512) / 1024));
	}
	if (mail_status)
		mail_status(line);
}

static void prog_tick(void)
{
	if (pf_ms() - prog.last >= 250)
		prog_show();
}

/* start showing progress; total: expected bytes (0 unknown), nitems: a
   count of things to come (0: count bytes) */
static void prog_start(int out, long total, long nitems, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(prog.label, sizeof(prog.label), fmt, ap);
	va_end(ap);
	prog.out = out;
	prog.base = out ? conn_bytes_out : conn_bytes;
	prog.total = total;
	prog.items = 0;
	prog.nitems = nitems;
	prog.on = 1;
	conn_tick = prog_tick;
	prog_show();
}

static void prog_item(void)
{
	if (!prog.on)
		return;
	prog.items++;
	if (prog.items == prog.nitems || pf_ms() - prog.last >= 250)
		prog_show();
}

static void prog_end(void)
{
	prog.on = 0;
	conn_tick = 0;
	status("");			/* done: the line never says it still works */
}

static int offline(void)
{
	if (opt.offline) {
		str_copy(mail_err, "EMail is working offline (Options menu).", sizeof(mail_err));
		mail_unreachable = 1;
		return 1;
	}
	return 0;
}

/* how long something took, for the protocol log: where the time goes */
static void timing(unsigned long t0, long b0, const char *fmt, ...)
{
	char what[120], line[180];
	unsigned long ms = pf_ms() - t0;
	long kb = (conn_bytes - b0 + 512) / 1024;
	va_list ap;
	if (!conn_logfile[0])
		return;
	va_start(ap, fmt);
	vsnprintf(what, sizeof(what), fmt, ap);
	va_end(ap);
	snprintf(line, sizeof(line), "%s: %ld KB in %lu.%02lu s", what, kb, ms / 1000, ms % 1000 / 10);
	conn_log("IMAP", " -- ", line, (long)strlen(line));
}

/* ---------------- IMAP session ---------------- */

void mail_disconnect(ACCOUNT *a)
{
	if (a->im) {
		imap_logout(a->im);
		a->im = 0;
	}
}

void mail_disconnect_all(void)
{
	short i;
	for (i = 0; i < naccts; i++)
		mail_disconnect(accts[i]);
}

/* after a failed command: drop the connection if it died */
static int imap_failed(ACCOUNT *a, int r)
{
	if (r < 0) {
		snprintf(mail_err, sizeof(mail_err), "%s: %s", a->name, a->im ? a->im->err : "connection lost");
		mail_disconnect(a);
	} else if (a->im) {
		snprintf(mail_err, sizeof(mail_err), "%s: %s", a->name, a->im->err);
	}
	return 0;
}

int mail_connect(ACCOUNT *a)
{
	mail_unreachable = 0;
	if (a->pop)
		return 1;
	if (offline())
		return 0;
	if (a->im) {
		/* reuse a connection used in the last few minutes; ask an older
		   one whether it is still there. Gateways, routers and providers
		   drop idle connections without a word, so the answer gets 10
		   seconds, not the usual minute: a live one replies at once, and
		   a slow one taken for dead costs a reconnect, not an error */
		int alive = pf_ms() - a->im_used < 120000UL;
		if (!alive) {
			long keep = a->im->c->timeout_ms;
			a->im->c->timeout_ms = 10000;
			alive = imap_noop(a->im) > 0;
			if (a->im)
				a->im->c->timeout_ms = keep;
			if (!alive && a->im) {
				conn_log("IMAP", " -- ", "the idle connection was dropped: connecting again", 50);
				a->im->c->dead = 1;
			}
		}
		if (alive) {
			a->im_used = pf_ms();
			return 1;
		}
		mail_disconnect(a);
	}
	if (a->cut) {
		/* don't try again on every click: Check mail does */
		snprintf(mail_err, sizeof(mail_err), "%s is not connected: %s", a->name, a->cuterr);
		mail_unreachable = 1;
		return 0;
	}
	status(acct_sec(a, 0) ? "%s: connecting securely to %s..." : "%s: connecting to %s...",
	       a->name, acct_host(a, 0));
	a->im = imap_login(acct_host(a, 0), acct_port(a, 0), acct_sec(a, 0), acct_user(a, 0), acct_pass(a, 0),
			   mail_err, sizeof(mail_err));
	if (!a->im) {
		a->cut = 1;
		str_copy(a->cuterr, mail_err, sizeof(a->cuterr));
		mail_unreachable = 1;
		status("%s: not connected, working from the cache", a->name);
		return 0;
	}
	a->cut = 0;
	a->im_used = pf_ms();
	status("");			/* connected: "connecting to..." is over */
	return 1;
}

static int select_folder(ACCOUNT *a, FINFO *fi)
{
	int r;
	if (!mail_connect(a))
		return 0;
	if (!strcmp(a->im->selected, fi->server))
		return 1;
	r = imap_select(a->im, fi->server, 0);
	if (r <= 0)
		return imap_failed(a, r);
	return 1;
}

/* "1:4,7,9:12" for up to max uids starting at list[*pos] */
static void uidset(const unsigned long *list, long n, long *pos, char *out, int size, int max)
{
	long i = *pos, cnt = 0;
	int o = 0;
	out[0] = 0;
	while (i < n && cnt < max && o < size - 24) {
		long j = i;
		while (j + 1 < n && list[j + 1] == list[j] + 1 && cnt + (j - i) + 1 < max)
			j++;
		if (o)
			out[o++] = ',';
		if (j > i)
			o += snprintf(out + o, size - o, "%lu:%lu", list[i], list[j]);
		else
			o += snprintf(out + o, size - o, "%lu", list[i]);
		cnt += j - i + 1;
		i = j + 1;
	}
	*pos = i;
}

/* ---------------- folders ---------------- */

typedef struct {
	ACCOUNT *a;
	char seen[MAXFOLDER];
} LISTCTX;

static void list_cb(void *ud, const char *name, char delim, int role, int noselect)
{
	LISTCTX *l = ud;
	FINFO *f = folder_find(l->a, name);
	if (!f || !f->local) {
		f = folder_add(l->a, name, delim, (short)role, (short)noselect, 0);
		if (f)
			l->seen[f - l->a->folders] = 1;
	}
}

int mail_refresh_folders(ACCOUNT *a)
{
	LISTCTX l;
	short i;
	int r;
	unsigned long t0;
	long b0;
	if (a->pop)
		return 1;
	if (!mail_connect(a))
		return 0;
	t0 = pf_ms();
	b0 = conn_bytes;
	prog_start(0, 0, 0, "%s: reading the folder list", a->name);
	memset(&l, 0, sizeof(l));
	l.a = a;
	for (i = 0; i < a->nfolders; i++)
		if (a->folders[i].local)
			l.seen[i] = 1;
	r = imap_list(a->im, list_cb, &l);
	prog_end();
	if (r <= 0)
		return imap_failed(a, r);
	/* drop folders that are gone from the server */
	for (i = a->nfolders - 1; i >= 0; i--) {
		if (!l.seen[i]) {
			short j;
			for (j = i; j < a->nfolders - 1; j++) {
				a->folders[j] = a->folders[j + 1];
				l.seen[j] = l.seen[j + 1];
			}
			a->nfolders--;
		}
	}
	folders_save(a);
	timing(t0, b0, "folder list, %d folders", a->nfolders);
	status("%s: %d folders", a->name, a->nfolders);
	return 1;
}

/* ---------------- IMAP mirror ---------------- */

typedef struct {
	unsigned long *uid;
	unsigned short *flags;
	long n, cap;
} UIDLIST;

static void uidflags_cb(void *ud, IMAPFETCH *f)
{
	UIDLIST *u = ud;
	if (u->n == u->cap) {
		long cap = u->cap ? u->cap * 2 : 256;
		unsigned long *nu = realloc(u->uid, cap * sizeof(unsigned long));
		unsigned short *nf;
		if (!nu)
			return;
		u->uid = nu;
		nf = realloc(u->flags, cap * sizeof(unsigned short));
		if (!nf)
			return;
		u->flags = nf;
		u->cap = cap;
	}
	u->uid[u->n] = f->uid;
	u->flags[u->n++] = f->flags;
	prog_item();
}

/* sort the (uid, flags) pairs by uid */
static void qsort_pairs(UIDLIST *u)
{
	long i, j;
	for (i = 1; i < u->n; i++) {		/* nearly always sorted already */
		unsigned long uu = u->uid[i];
		unsigned short ff = u->flags[i];
		for (j = i; j > 0 && u->uid[j - 1] > uu; j--) {
			u->uid[j] = u->uid[j - 1];
			u->flags[j] = u->flags[j - 1];
		}
		u->uid[j] = uu;
		u->flags[j] = ff;
	}
}

static long find_uid(UIDLIST *u, unsigned long uid)
{
	long lo = 0, hi = u->n - 1;
	while (lo <= hi) {
		long mid = (lo + hi) / 2;
		if (u->uid[mid] == uid)
			return mid;
		if (u->uid[mid] < uid)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

typedef struct {
	FOLDER *f;
	long added, unseen;
} HDRCTX;

static void header_cb(void *ud, IMAPFETCH *im)
{
	HDRCTX *c = ud;
	HDR *h = fold_add(c->f, im->uid);
	if (!h)
		return;
	h->flags = im->flags;
	h->size = im->size;
	if (im->hdr)
		hdr_from_raw(h, im->hdr, im->hdrlen);
	else
		hdr_set(h, "", "", "");
	if (!h->date && im->internaldate[0])
		h->date = mime_date(im->internaldate);
	c->added++;
	if (!(h->flags & MF_SEEN))
		c->unseen++;
	prog_item();
}

/* Mirror the newest f->window messages of a folder (opt.page to start
 * with, more with mail_load_more): their flags, and the headers of the
 * ones we don't have yet. Older messages stay on the server only, so a
 * Sent folder with 8000 messages costs no more than one with 100. */
static long flags_pushed;

static int sync_window(ACCOUNT *a, FINFO *fi, FOLDER *f, long *newmsgs)
{
	UIDLIST u;
	HDRCTX hc;
	long i, j, pos, first, n;
	unsigned long *want;
	char range[40];
	int r;
	unsigned long t0 = pf_ms();
	long b0 = conn_bytes;

	status("%s: opening %s...", a->name, fi->disp);
	r = imap_select(a->im, fi->server, 0);
	if (r <= 0)
		return imap_failed(a, r);
	if (f->uidvalidity != a->im->uidvalidity) {
		/* the server renumbered the folder: start over */
		fold_clear(f);
		f->uidvalidity = a->im->uidvalidity;
		f->dirty = 1;
	}
	n = (long)a->im->exists;
	f->exists = n;
	f->uidnext = a->im->uidnext;
	f->dirty = 1;
	if (f->window < opt.page)
		f->window = opt.page;
	if (n == 0) {
		fold_clear(f);
		return 1;
	}

	/* UIDs and flags of the newest `window` messages */
	first = n > f->window ? n - f->window + 1 : 1;
	snprintf(range, sizeof(range), "%ld:%ld", first, n);
	memset(&u, 0, sizeof(u));
	prog_start(0, 0, n - first + 1, "%s: checking %s -", a->name, fi->disp);
	r = imap_fetch_seq(a->im, range, "(UID FLAGS)", uidflags_cb, &u);
	prog_end();
	if (r <= 0) {
		free(u.uid);
		free(u.flags);
		return imap_failed(a, r);
	}
	qsort_pairs(&u);

	/* read and flagged changes made while not connected go to the server
	   first, or the server's flags would undo them */
	for (i = 0; i < f->n && r > 0; i++) {
		HDR *h = &f->h[i];
		long k;
		unsigned short d, bit;
		if (!(h->flags & MF_FLAGSYNC) || (k = find_uid(&u, h->uid)) < 0)
			continue;
		d = (h->flags ^ u.flags[k]) & (MF_SEEN | MF_FLAGGED | MF_ANSWERED);
		for (bit = 1; bit && r > 0; bit <<= 1)
			if (d & bit) {
				char set[16];
				snprintf(set, sizeof(set), "%lu", h->uid);
				r = imap_store(a->im, set, (h->flags & bit) != 0, bit);
			}
		if (r > 0) {
			u.flags[k] = (u.flags[k] & ~d) | (h->flags & d);
			flags_pushed++;
		}
	}
	if (r <= 0) {
		free(u.uid);
		free(u.flags);
		return imap_failed(a, r);
	}

	/* drop what was expunged or fell out of the window; update flags */
	for (i = f->n - 1; i >= 0; i--) {
		HDR *h = &f->h[i];
		long k = find_uid(&u, h->uid);
		if (k < 0) {
			fold_remove(f, h->uid);
		} else {
			unsigned short nf = u.flags[k] | (h->flags & (MF_ATTACH | MF_CACHED));
			if (nf != h->flags) {
				h->flags = nf;
				f->dirty = 1;
			}
		}
	}

	/* headers of the ones we don't have */
	want = malloc((u.n + 1) * sizeof(unsigned long));
	j = 0;
	if (want)
		for (i = 0; i < u.n; i++)
			if (!fold_get(f, u.uid[i]))
				want[j++] = u.uid[i];
	hc.f = f;
	hc.added = hc.unseen = 0;
	r = 1;
	if (j)
		prog_start(0, 0, j, "%s: %s - headers", a->name, fi->disp);
	for (pos = 0; pos < j && r > 0; ) {
		char set[700];
		uidset(want, j, &pos, set, sizeof(set), 50);
		r = imap_fetch(a->im, set,
			       "(UID FLAGS RFC822.SIZE INTERNALDATE BODY.PEEK[HEADER.FIELDS (FROM TO SUBJECT DATE CONTENT-TYPE)])",
			       header_cb, &hc);
	}
	prog_end();
	if (newmsgs)
		*newmsgs = hc.unseen;
	if (r > 0)
		timing(t0, b0, "%s: %ld flags, %ld new headers", fi->disp, u.n, j);
	free(want);
	free(u.uid);
	free(u.flags);
	if (r <= 0)
		return imap_failed(a, r);
	return 1;
}

int mail_sync_folder(ACCOUNT *a, FINFO *fi, long *newmsgs)
{
	FOLDER *f;
	long messages = -1, unseen = -1;
	int ok;

	if (newmsgs)
		*newmsgs = 0;
	if (fi->local || fi->noselect)
		return 1;
	if (!mail_connect(a))
		return 0;
	/* the folder's real counts, cheaply */
	if (imap_status(a->im, fi->server, &messages, &unseen) < 0)
		return imap_failed(a, -1);
	f = fold_open(a, fi);
	if (!f) {
		str_copy(mail_err, "out of memory", sizeof(mail_err));
		return 0;
	}
	flags_pushed = 0;
	ok = sync_window(a, fi, f, newmsgs);
	if (ok && flags_pushed)		/* the counts changed with them */
		imap_status(a->im, fi->server, &messages, &unseen);
	if (ok)
		fi->synced = pf_ms() | 1;
	if (messages >= 0)
		fi->total = messages;
	if (unseen >= 0)
		fi->unread = unseen;
	fold_close(f);
	folders_save(a);
	if (ok)
		status("");
	return ok;
}

int mail_load_more(ACCOUNT *a, FINFO *fi, long *added)
{
	FOLDER *f;
	long before;
	int ok;
	if (added)
		*added = 0;
	if (fi->local || !mail_connect(a))
		return 0;
	f = fold_open(a, fi);
	if (!f)
		return 0;
	before = f->n;
	f->window = (f->window > f->n ? f->n : f->window) + opt.page;
	ok = sync_window(a, fi, f, 0);
	if (added)
		*added = f->n - before;
	fold_close(f);
	if (ok)
		status("");
	return ok;
}

/* ---------------- POP3 ---------------- */

typedef struct {
	ACCOUNT *a;
	POP3 *p;
	FOLDER *f;
	char *known;		/* UIDL.DAT contents */
	SBUF keep;		/* new UIDL.DAT */
	long got, fail, total, done;
} POPCTX;

static void pop_cb(void *ud, long num, const char *uid, long size)
{
	POPCTX *c = ud;
	char key[100];
	SBUF msg;
	HDR *h;
	int r;
	snprintf(key, sizeof(key), "\n%s\n", uid);
	if (c->known && strstr(c->known, key)) {
		/* already downloaded */
		sb_adds(&c->keep, uid);
		sb_adds(&c->keep, "\n");
		return;
	}
	if (c->fail)
		return;
	prog_start(0, size, 0, "%s: downloading message %ld", c->a->name, c->got + 1);
	sb_init(&msg);
	r = pop3_retr(c->p, num, &msg);
	prog_end();
	if (r <= 0) {
		c->fail = 1;
		sb_free(&msg);
		return;
	}
	h = fold_add(c->f, c->f->uidnext);
	if (h) {
		msg_save(c->f, c->f->uidnext, msg.s, msg.len);
		hdr_from_raw(h, msg.s, msg.len);
		h->size = msg.len;
		h->flags |= MF_CACHED;
		c->f->uidnext++;
		c->got++;
		if (c->a->leave) {
			sb_adds(&c->keep, uid);
			sb_adds(&c->keep, "\n");
		} else {
			pop3_dele(c->p, num);
		}
	}
	sb_free(&msg);
}

static int pop_check(ACCOUNT *a, long *newmsgs)
{
	POPCTX c;
	FINFO *inbox = folder_role(a, FR_INBOX);
	char path[220];
	int r;
	if (!inbox)
		inbox = folder_add(a, "Inbox", 0, FR_INBOX, 0, 1);
	if (offline())
		return 0;
	memset(&c, 0, sizeof(c));
	c.a = a;
	status(acct_sec(a, 0) ? "%s: connecting securely to %s..." : "%s: connecting to %s...",
	       a->name, acct_host(a, 0));
	c.p = pop3_login(acct_host(a, 0), acct_port(a, 0), acct_sec(a, 0), acct_user(a, 0), acct_pass(a, 0),
			 mail_err, sizeof(mail_err));
	if (!c.p)
		return 0;
	c.f = fold_open(a, inbox);
	path_join(path, sizeof(path), a->dir, "UIDL.DAT");
	{
		char *k = pf_load(path, 0);
		SBUF kb;
		sb_init(&kb);
		sb_adds(&kb, "\n");
		if (k) {
			sb_adds(&kb, k);
			free(k);
		}
		sb_adds(&kb, "\n");
		c.known = sb_steal(&kb);
	}
	sb_init(&c.keep);
	r = pop3_list(c.p, pop_cb, &c);
	if (r <= 0 || c.fail)
		snprintf(mail_err, sizeof(mail_err), "%s: %s", a->name, c.p->err);
	pop3_quit(c.p);
	pf_save(path, c.keep.s ? c.keep.s : "", c.keep.len);
	sb_free(&c.keep);
	free(c.known);
	fold_close(c.f);
	folders_save(a);
	if (newmsgs)
		*newmsgs = c.got;
	return r > 0 && !c.fail;
}

int mail_check(ACCOUNT *a, long *newmsgs)
{
	int ok, sent;
	a->cut = 0;			/* checking mail is how to try again */
	if (a->pop) {
		ok = pop_check(a, newmsgs);
	} else {
		FINFO *in;
		if (!a->nfolders || !folder_role(a, FR_INBOX)) {
			if (!mail_refresh_folders(a))
				return 0;
		}
		in = folder_role(a, FR_INBOX);
		ok = in ? mail_sync_folder(a, in, newmsgs) : 0;
	}
	if (ok) {
		FINFO *ob = folder_role(a, FR_OUTBOX);
		if (ob && ob->total > 0)
			ok = mail_send_outbox(a, &sent) >= 0;
	}
	return ok;
}

/* ---------------- reading and changing ---------------- */

typedef struct {
	SBUF body;
} BODYCTX;

static void body_cb(void *ud, IMAPFETCH *f)
{
	BODYCTX *c = ud;
	if (f->body)
		sb_add(&c->body, f->body, f->bodylen);
}

char *mail_fetch(FOLDER *f, HDR *h, long *len)
{
	char *raw = 0;
	ACCOUNT *a = f->acct;
	if (h->flags & MF_CACHED || f->fi->local)
		raw = msg_load(f, h->uid, len);
	if (!raw && !f->fi->local) {
		BODYCTX c;
		char set[16];
		int r;
		unsigned long t0;
		long b0;
		if (!select_folder(a, f->fi))
			return 0;
		prog_start(0, h->size, 0, "%s: downloading the message", a->name);
		t0 = pf_ms();
		b0 = conn_bytes;
		sb_init(&c.body);
		snprintf(set, sizeof(set), "%lu", h->uid);
		r = imap_fetch(a->im, set, "(UID BODY.PEEK[])", body_cb, &c);
		prog_end();
		if (r <= 0) {
			sb_free(&c.body);
			imap_failed(a, r);
			return 0;
		}
		if (!c.body.s) {
			str_copy(mail_err, "The message is no longer on the server.", sizeof(mail_err));
			return 0;
		}
		timing(t0, b0, "message %lu", h->uid);
		msg_save(f, h->uid, c.body.s, c.body.len);
		h->flags |= MF_CACHED;
		f->dirty = 1;
		if (len)
			*len = c.body.len;
		raw = sb_steal(&c.body);
		status("");
	}
	if (!raw) {
		if (!mail_err[0])
			str_copy(mail_err, "The message is not in the cache.", sizeof(mail_err));
		return 0;
	}
	if (!(h->flags & MF_SEEN))
		mail_flag(f, h, MF_SEEN, 1);
	return raw;
}

int mail_flag(FOLDER *f, HDR *h, unsigned short flags, int add)
{
	unsigned short nf = add ? (h->flags | flags) : (h->flags & ~flags);
	/* changed here at once; the server hears of it in one batch when
	   EMail is idle, at the next sync or at quit (mail_push_flags), so a
	   click never waits for the network */
	if (!f->fi->local && nf != h->flags)
		nf |= MF_FLAGSYNC;
	if (nf != h->flags) {
		/* IMAP counts come from the server: keep them in step */
		if (!f->fi->local && ((nf ^ h->flags) & MF_SEEN))
			f->fi->unread += (nf & MF_SEEN) ? -1 : 1;
		if (f->fi->unread < 0)
			f->fi->unread = 0;
		h->flags = nf;
		f->dirty = 1;
		fold_count(f);
	}
	return 1;
}

int mail_flags_pending(FOLDER *f)
{
	long i;
	if (!f || f->fi->local)
		return 0;
	for (i = 0; i < f->n; i++)
		if (f->h[i].flags & MF_FLAGSYNC)
			return 1;
	return 0;
}

int mail_push_flags(FOLDER *f)
{
	static const unsigned short bits[3] = { MF_SEEN, MF_FLAGGED, MF_ANSWERED };
	ACCOUNT *a;
	unsigned long *list;
	long i, n, pos;
	short b, add;
	int r = 1;
	if (!mail_flags_pending(f) || opt.offline || f->acct->cut)
		return 1;
	a = f->acct;
	if (!select_folder(a, f->fi))
		return mail_unreachable;	/* not connected: they wait */
	list = malloc(f->n * sizeof(unsigned long));
	if (!list)
		return 0;
	/* one STORE for each flag set and each flag cleared */
	for (b = 0; b < 3 && r > 0; b++)
		for (add = 0; add < 2 && r > 0; add++) {
			n = 0;
			for (i = 0; i < f->n; i++) {
				HDR *h = &f->h[i];
				if ((h->flags & MF_FLAGSYNC) && ((h->flags & bits[b]) != 0) == add)
					list[n++] = h->uid;
			}
			for (pos = 0; pos < n && r > 0; ) {
				char set[700];
				uidset(list, n, &pos, set, sizeof(set), 50);
				r = imap_store(a->im, set, add, bits[b]);
			}
		}
	free(list);
	if (r <= 0)
		return imap_failed(a, r);
	for (i = 0; i < f->n; i++)
		f->h[i].flags &= ~MF_FLAGSYNC;
	f->dirty = 1;
	return 1;
}

/* an IMAP message left this folder */
static void count_gone(FOLDER *f, HDR *h)
{
	if (f->fi->local)
		return;
	if (f->fi->total > 0)
		f->fi->total--;
	if (!(h->flags & MF_SEEN) && f->fi->unread > 0)
		f->fi->unread--;
	if (f->exists > 0)
		f->exists--;
}

int mail_mark_all_read(ACCOUNT *a, FINFO *fi, FOLDER *open)
{
	FOLDER *f;
	long i;
	int later = 0;			/* not connected: tell the server next time */
	if (!fi->local) {
		int r;
		if (opt.offline || a->cut || !select_folder(a, fi)) {
			if (!opt.offline && !a->cut && !mail_unreachable)
				return 0;
			later = 1;
		} else if (a->im->exists) {
			r = imap_store_seq(a->im, "1:*", 1, MF_SEEN);
			if (r <= 0)
				return imap_failed(a, r);
		}
		fi->unread = 0;
	}
	/* the folder on the screen is changed itself: changing a copy of it
	   on disk would be undone when the screen's copy is saved */
	f = open ? open : fold_open(a, fi);
	if (!f)
		return 0;
	for (i = 0; i < f->n; i++) {
		if (!(f->h[i].flags & MF_SEEN)) {
			f->h[i].flags |= MF_SEEN | (later ? MF_FLAGSYNC : 0);
			f->dirty = 1;
		}
	}
	fold_count(f);
	if (!open)
		fold_close(f);
	folders_save(a);
	return 1;
}

/* copy a local message file into another local folder */
static int local_copy(FOLDER *f, HDR *h, FINFO *dest)
{
	FOLDER *d = fold_open(f->acct, dest);
	long len;
	char *raw = msg_load(f, h->uid, &len);
	HDR *nh;
	if (!d || !raw) {
		free(raw);
		fold_close(d);
		str_copy(mail_err, "Can't copy the message.", sizeof(mail_err));
		return 0;
	}
	nh = fold_add(d, d->uidnext);
	if (nh) {
		msg_save(d, d->uidnext, raw, len);
		nh->flags = h->flags | MF_CACHED;
		nh->size = h->size;
		nh->date = h->date;
		hdr_set(nh, h->from, h->subject, h->to);
		d->uidnext++;
	}
	free(raw);
	fold_close(d);
	return nh != 0;
}

int mail_delete(FOLDER *f, HDR *h)
{
	ACCOUNT *a = f->acct;
	FINFO *trash = folder_role(a, FR_TRASH);
	unsigned long uid = h->uid;
	mail_push_flags(f);		/* the copy in Trash keeps them */
	if (f->fi->local) {
		if (trash && trash->local && trash != f->fi && f->fi->role != FR_OUTBOX)
			if (!local_copy(f, h, trash))
				return 0;
	} else {
		char set[16];
		int r;
		if (!select_folder(a, f->fi))
			return 0;
		snprintf(set, sizeof(set), "%lu", uid);
		if (trash && !trash->local && trash != f->fi) {
			r = imap_move(a->im, set, trash->server);
		} else {
			r = imap_store(a->im, set, 1, MF_DELETED);
			if (r > 0)
				r = imap_expunge(a->im, set);
		}
		if (r <= 0)
			return imap_failed(a, r);
		if (trash && !trash->local && trash != f->fi)
			trash->total++;
		count_gone(f, h);
	}
	fold_remove(f, uid);
	fold_count(f);
	return 1;
}

int mail_move(FOLDER *f, HDR *h, FINFO *dest)
{
	ACCOUNT *a = f->acct;
	unsigned long uid = h->uid;
	if (dest == f->fi)
		return 1;
	mail_push_flags(f);		/* the moved copy keeps them */
	if (f->fi->local != dest->local) {
		str_copy(mail_err, "Messages can only move between folders of the same kind.", sizeof(mail_err));
		return 0;
	}
	if (f->fi->local) {
		if (!local_copy(f, h, dest))
			return 0;
	} else {
		char set[16];
		int r;
		if (!select_folder(a, f->fi))
			return 0;
		snprintf(set, sizeof(set), "%lu", uid);
		r = imap_move(a->im, set, dest->server);
		if (r <= 0)
			return imap_failed(a, r);
		dest->total++;
		if (!(h->flags & MF_SEEN))
			dest->unread++;
		count_gone(f, h);
	}
	fold_remove(f, uid);
	fold_count(f);
	return 1;
}

/* ---------------- sending ---------------- */

int mail_queue(ACCOUNT *a, const char *raw, long len)
{
	FINFO *ob = folder_role(a, FR_OUTBOX);
	FOLDER *f;
	HDR *h;
	if (!ob)
		ob = folder_add(a, "Outbox", 0, FR_OUTBOX, 0, 1);
	f = fold_open(a, ob);
	if (!f)
		return 0;
	h = fold_add(f, f->uidnext);
	if (!h || msg_save(f, f->uidnext, raw, len) < 0) {
		fold_close(f);
		str_copy(mail_err, "Can't write to the Outbox (disk full?)", sizeof(mail_err));
		return 0;
	}
	hdr_from_raw(h, raw, len);
	h->size = len;
	h->flags = MF_SEEN | MF_CACHED;
	f->uidnext++;
	fold_close(f);
	folders_save(a);
	return 1;
}

int mail_send_outbox(ACCOUNT *a, int *sent)
{
	FINFO *ob = folder_role(a, FR_OUTBOX), *sentf;
	FOLDER *f;
	SMTP *s;
	const char *user, *pass;
	char helo[64];
	long i, nsend;
	int ok = 1;

	*sent = 0;
	if (!ob)
		return 0;
	if (offline())
		return -1;
	f = fold_open(a, ob);
	if (!f || !f->n) {
		fold_close(f);
		return 0;
	}
	user = acct_user(a, 1);
	pass = acct_pass(a, 1);
	if (!user)
		user = "";
	{
		const char *at = strchr(a->email, '@');
		str_copy(helo, at ? at + 1 : "atari.local", sizeof(helo));
	}
	status(acct_sec(a, 1) ? "%s: connecting securely to %s..." : "%s: connecting to %s...",
	       a->name, acct_host(a, 1));
	s = smtp_open(acct_host(a, 1), acct_port(a, 1), acct_sec(a, 1), helo, user, pass,
		      mail_err, sizeof(mail_err));
	if (!s) {
		fold_close(f);
		return -1;
	}
	sentf = folder_role(a, FR_SENT);
	nsend = f->n;
	for (i = f->n - 1; i >= 0; i--) {
		HDR *h = &f->h[i];
		long len, sl;
		char *raw = msg_load(f, h->uid, &len), *out, rcpt[40][96];
		const char *rp[40];
		int n, k, r;
		if (!raw)
			continue;
		n = compose_rcpts(raw, len, rcpt, 40);
		for (k = 0; k < n; k++)
			rp[k] = rcpt[k];
		out = compose_for_smtp(raw, len, &sl);
		prog_start(1, sl, 0, "%s: sending %s of %s, \"%.24s\"", a->name,
			   num(nsend - i), num(nsend), h->subject);
		r = smtp_send(s, a->email, rp, n, out, sl);
		prog_end();
		if (r <= 0) {
			snprintf(mail_err, sizeof(mail_err), "Sending \"%.40s\" failed: %s", h->subject, s->err);
			free(raw);
			free(out);
			ok = -1;
			if (r < 0)
				break;
			continue;
		}
		(*sent)++;
		/* keep a copy in Sent (Gmail files it by itself) */
		if (sentf && sentf->local) {
			local_copy(f, h, sentf);
		} else if (sentf && !str_istr(acct_host(a, 1), "gmail") && !str_istr(acct_host(a, 0), "gmail") &&
			   mail_connect(a)) {
			prog_start(1, sl, 0, "%s: saving a copy in %s", a->name, sentf->disp);
			imap_append(a->im, sentf->server, MF_SEEN, out, sl);
			prog_end();
		}
		free(raw);
		free(out);
		fold_remove(f, h->uid);
	}
	smtp_quit(s);
	fold_close(f);
	folders_save(a);
	status("");
	return ok;
}

/* ---------------- folder management ---------------- */

int mail_folder_create(ACCOUNT *a, const char *name)
{
	char *m = atari_to_mutf7(name);
	int r;
	if (!m || a->pop || !mail_connect(a)) {
		free(m);
		return 0;
	}
	r = imap_create(a->im, m);
	free(m);
	if (r <= 0)
		return imap_failed(a, r);
	return mail_refresh_folders(a);
}

int mail_folder_delete(ACCOUNT *a, FINFO *fi)
{
	int r;
	if (fi->local || fi->role || !mail_connect(a)) {
		if (fi->local || fi->role)
			str_copy(mail_err, "This folder can't be deleted.", sizeof(mail_err));
		return 0;
	}
	if (!strcmp(a->im->selected, fi->server)) {
		imap_select(a->im, "INBOX", 1);
	}
	r = imap_delete(a->im, fi->server);
	if (r <= 0)
		return imap_failed(a, r);
	return mail_refresh_folders(a);
}
