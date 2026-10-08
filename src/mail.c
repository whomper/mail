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

char mail_err[200];
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

static int offline(void)
{
	if (opt.offline) {
		str_copy(mail_err, "MAIL is working offline (Options menu).", sizeof(mail_err));
		return 1;
	}
	return 0;
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
	if (a->pop)
		return 1;
	if (offline())
		return 0;
	if (a->im) {
		/* reuse a connection used in the last few minutes; check older ones */
		if (pf_ms() - a->im_used < 120000UL || imap_noop(a->im) > 0) {
			a->im_used = pf_ms();
			return 1;
		}
		mail_disconnect(a);
	}
	status("%s: connecting to %s...", a->name, a->host);
	a->im = imap_login(a->host, a->port, a->user, a->pass, mail_err, sizeof(mail_err));
	if (!a->im)
		return 0;
	a->im_used = pf_ms();
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
	if (a->pop)
		return 1;
	if (!mail_connect(a))
		return 0;
	status("%s: reading the folder list...", a->name);
	memset(&l, 0, sizeof(l));
	l.a = a;
	for (i = 0; i < a->nfolders; i++)
		if (a->folders[i].local)
			l.seen[i] = 1;
	r = imap_list(a->im, list_cb, &l);
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
}

/* Mirror the newest f->window messages of a folder (opt.page to start
 * with, more with mail_load_more): their flags, and the headers of the
 * ones we don't have yet. Older messages stay on the server only, so a
 * Sent folder with 8000 messages costs no more than one with 100. */
static int sync_window(ACCOUNT *a, FINFO *fi, FOLDER *f, long *newmsgs)
{
	UIDLIST u;
	HDRCTX hc;
	long i, j, pos, first, n;
	unsigned long *want;
	char range[40];
	int r;

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
	status("%s: checking %s...", a->name, fi->disp);
	r = imap_fetch_seq(a->im, range, "(UID FLAGS)", uidflags_cb, &u);
	if (r <= 0) {
		free(u.uid);
		free(u.flags);
		return imap_failed(a, r);
	}
	qsort_pairs(&u);

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
	for (pos = 0; pos < j && r > 0; ) {
		char set[700];
		uidset(want, j, &pos, set, sizeof(set), 50);
		status("%s: %s - headers %ld of %ld", a->name, fi->disp, pos, j);
		r = imap_fetch(a->im, set,
			       "(UID FLAGS RFC822.SIZE INTERNALDATE BODY.PEEK[HEADER.FIELDS (FROM TO SUBJECT DATE CONTENT-TYPE)])",
			       header_cb, &hc);
	}
	if (newmsgs)
		*newmsgs = hc.unseen;
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
	ok = sync_window(a, fi, f, newmsgs);
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
	(void)size;
	snprintf(key, sizeof(key), "\n%s\n", uid);
	if (c->known && strstr(c->known, key)) {
		/* already downloaded */
		sb_adds(&c->keep, uid);
		sb_adds(&c->keep, "\n");
		return;
	}
	if (c->fail)
		return;
	status("%s: downloading message %ld...", c->a->name, c->got + 1);
	sb_init(&msg);
	if (pop3_retr(c->p, num, &msg) <= 0) {
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
	status("%s: connecting to %s...", a->name, a->host);
	c.p = pop3_login(a->host, a->port, a->user, a->pass, mail_err, sizeof(mail_err));
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
		if (!select_folder(a, f->fi))
			return 0;
		status("%s: downloading the message (%ld bytes)...", a->name, h->size);
		sb_init(&c.body);
		snprintf(set, sizeof(set), "%lu", h->uid);
		r = imap_fetch(a->im, set, "(UID BODY.PEEK[])", body_cb, &c);
		if (r <= 0) {
			sb_free(&c.body);
			imap_failed(a, r);
			return 0;
		}
		if (!c.body.s) {
			str_copy(mail_err, "The message is no longer on the server.", sizeof(mail_err));
			return 0;
		}
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
	if (!f->fi->local && !opt.offline) {
		char set[16];
		int r;
		if (!select_folder(f->acct, f->fi))
			return 0;
		snprintf(set, sizeof(set), "%lu", h->uid);
		r = imap_store(f->acct->im, set, add, flags);
		if (r <= 0)
			return imap_failed(f->acct, r);
	}
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

int mail_mark_all_read(ACCOUNT *a, FINFO *fi)
{
	FOLDER *f;
	long i;
	if (!fi->local) {
		int r;
		if (!select_folder(a, fi))
			return 0;
		if (a->im->exists) {
			r = imap_store_seq(a->im, "1:*", 1, MF_SEEN);
			if (r <= 0)
				return imap_failed(a, r);
		}
		fi->unread = 0;
	}
	f = fold_open(a, fi);
	if (!f)
		return 0;
	for (i = 0; i < f->n; i++) {
		if (!(f->h[i].flags & MF_SEEN)) {
			f->h[i].flags |= MF_SEEN;
			f->dirty = 1;
		}
	}
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
	long i;
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
	user = a->smtpuser[0] ? a->smtpuser : a->user;
	pass = a->smtpuser[0] ? a->smtppass : a->pass;
	if (!strcmp(user, "-"))
		user = "";
	{
		const char *at = strchr(a->email, '@');
		str_copy(helo, at ? at + 1 : "atari.local", sizeof(helo));
	}
	status("%s: connecting to %s...", a->name, a->smtphost[0] ? a->smtphost : a->host);
	s = smtp_open(a->smtphost[0] ? a->smtphost : a->host, a->smtpport, helo, user, pass,
		      mail_err, sizeof(mail_err));
	if (!s) {
		fold_close(f);
		return -1;
	}
	sentf = folder_role(a, FR_SENT);
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
		status("%s: sending \"%s\"...", a->name, h->subject);
		r = smtp_send(s, a->email, rp, n, out, sl);
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
		} else if (sentf && !str_istr(a->smtphost, "gmail") && !str_istr(a->host, "gmail") &&
			   mail_connect(a)) {
			imap_append(a->im, sentf->server, MF_SEEN, out, sl);
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
