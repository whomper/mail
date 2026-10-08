/*
 * dialogs.c - MAIL's dialogs, built in code (no .RSC file needed):
 * account setup, settings, a one-line question, list pickers, alerts.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "ui.h"
#include "../src/bidi.h"
#include "../src/util.h"

#define DMAX 72
#define TMAX 24

static OBJECT tree[DMAX];
static void make_eyes(void);
static short npw;
static TEDINFO ted[TMAX];
static char tmpl[TMAX][64], valid[TMAX][64];
static short nobj, nted, last_child;

/* ---------------- builder ---------------- */

static void d_begin(short w, short h)
{
	static short eyes_made;
	if (!eyes_made) {
		make_eyes();
		eyes_made = 1;
	}
	npw = 0;
	memset(tree, 0, sizeof(tree));
	nobj = 1;
	nted = 0;
	last_child = 0;
	tree[0].ob_next = -1;
	tree[0].ob_head = tree[0].ob_tail = -1;
	tree[0].ob_type = G_BOX;
	tree[0].ob_state = OUTLINED;
	tree[0].ob_spec = 0x00021100L;
	tree[0].ob_width = w;
	tree[0].ob_height = h;
}

static short d_add(short type, short flags, short state, long spec, short x, short y, short w, short h)
{
	short i = nobj++;
	OBJECT *o = &tree[i];
	o->ob_next = 0;
	o->ob_head = o->ob_tail = -1;
	o->ob_type = type;
	o->ob_flags = flags;
	o->ob_state = state;
	o->ob_spec = spec;
	o->ob_x = x;
	o->ob_y = y;
	o->ob_width = w;
	o->ob_height = h;
	if (last_child)
		tree[last_child].ob_next = i;
	else
		tree[0].ob_head = i;
	tree[0].ob_tail = i;
	last_child = i;
	return i;
}

static short d_text(short x, short y, const char *s)
{
	return d_add(G_STRING, 0, 0, (long)s, x, y, (short)strlen(s), 1);
}

/* an editable field showing len characters of buf (buf holds len+1) */
static short d_edit(short x, short y, char *buf, short len, char kind)
{
	TEDINFO *t = &ted[nted];
	short i;
	if (len > 60)
		len = 60;
	for (i = 0; i < len; i++) {
		tmpl[nted][i] = '_';
		valid[nted][i] = kind;
	}
	tmpl[nted][len] = valid[nted][len] = 0;
	buf[len] = 0;
	t->te_ptext = buf;
	t->te_ptmplt = tmpl[nted];
	t->te_pvalid = valid[nted];
	t->te_font = 3;
	t->te_just = 0;
	t->te_color = 0x1180;
	t->te_thickness = 0;
	t->te_txtlen = len + 1;
	t->te_tmplen = len + 1;
	nted++;
	return d_add(G_FTEXT, EDITABLE, 0, (long)t, x, y, len, 1);
}

static short d_button(short x, short y, short w, const char *s, short flags)
{
	return d_add(G_BUTTON, SELECTABLE | flags, 0, (long)s, x, y, w, 1);
}

static short d_check(short x, short y, const char *s, short on)
{
	return d_add(G_BUTTON, SELECTABLE, on ? SELECTED : 0, (long)s, x, y, (short)strlen(s) + 2, 1);
}

static void d_end(void)
{
	short i;
	tree[last_child].ob_next = 0;
	tree[nobj - 1].ob_flags |= LASTOB;
	for (i = 0; i < nobj; i++)
		rsrc_obfix(tree, i);
}

/* ---------------- password fields ----------------
 * GEM's text fields can't hide what they show, so a password field
 * shows a row of '*' while the real text is kept aside; the eye button
 * next to it shows it in clear. form_loop() below is form_do() written
 * out with form_keybd/form_button/objc_edit, so it can type into a
 * password field itself. */

#define PWMAX 2

typedef struct {
	short obj, eye;		/* the text field and its eye button */
	char *real;		/* the password */
	short max;
	char shown[48];		/* what the field displays */
	short clear;		/* 1: shown in clear */
} PWFIELD;

static PWFIELD pwf[PWMAX];

/* the eye: 16 pixels wide, 16 rows (8 on ST medium), open and crossed */
static const char *const eye_art[16] = {
	"................",
	"................",
	"................",
	".....######.....",
	"...##......##...",
	"..#....##....#..",
	".#....####....#.",
	"#.....####.....#",
	".#....####....#.",
	"..#....##....#..",
	"...##......##...",
	".....######.....",
	"................",
	"................",
	"................",
	"................",
};
static unsigned short eye_open[16], eye_shut[16];
static BITBLK eye_blk[PWMAX];

static void make_eyes(void)
{
	short tall = gl_hchar >= 16, r, rows = tall ? 16 : 8, c;
	for (r = 0; r < rows; r++) {
		const char *src = eye_art[tall ? r : r * 2 + 1];
		unsigned short v = 0;
		for (c = 0; c < 16; c++)
			v = (v << 1) | (src[c] == '#');
		eye_open[r] = v;
		/* crossed out: a diagonal from bottom left to top right */
		eye_shut[r] = v | (0x8000 >> ((rows - 1 - r) * 16 / rows)) | (0x4000 >> ((rows - 1 - r) * 16 / rows));
	}
}

static void pw_update_text(PWFIELD *p)
{
	short n = (short)strlen(p->real), i;
	if (n > p->max)
		n = p->max;
	for (i = 0; i < n; i++)
		p->shown[i] = p->clear ? p->real[i] : '*';
	p->shown[n] = 0;
	eye_blk[p - pwf].bi_pdata = p->clear ? eye_shut : eye_open;
}

/* a password field of len characters on real (which holds len+1) */
static short d_pass(short x, short y, char *real, short len)
{
	PWFIELD *p = &pwf[npw];
	BITBLK *b = &eye_blk[npw];
	short f;
	if (len > 46)
		len = 46;
	p->real = real;
	p->max = len;
	p->clear = 0;
	real[len] = 0;
	pw_update_text(p);
	f = d_edit(x, y, p->shown, len, 'X');
	p->obj = f;
	b->bi_wb = 2;
	b->bi_hl = gl_hchar >= 16 ? 16 : 8;
	b->bi_x = b->bi_y = 0;
	b->bi_color = 1;
	p->eye = d_add(G_IMAGE, TOUCHEXIT, 0, (long)b, x + len + 1, y, 2, 1);
	npw++;
	return f;
}

static PWFIELD *pw_of(short obj)
{
	short i;
	for (i = 0; i < npw; i++)
		if (pwf[i].obj == obj)
			return &pwf[i];
	return 0;
}

static void draw_obj(short obj)
{
	short x, y;
	objc_offset(tree, obj, &x, &y);
	objc_draw(tree, obj, 0, x - 2, y - 2, tree[obj].ob_width + 4, tree[obj].ob_height + 4);
}

/* type into a password field; 1 if the key was used */
static int pw_key(PWFIELD *p, short key, short *idx)
{
	short scan = KEY_SCAN(key), ascii = KEY_ASCII(key), n = (short)strlen(p->real);
	if (scan == 0x0e) {			/* Backspace */
		if (n)
			p->real[n - 1] = 0;
	} else if (scan == 0x01 || scan == 0x53) {	/* Esc, Delete: clear */
		p->real[0] = 0;
	} else if (scan == 0x4b || scan == 0x4d) {	/* the cursor stays at the end */
		return 1;
	} else if (ascii >= 32 && n < p->max) {
		p->real[n] = (char)ascii;
		p->real[n + 1] = 0;
	} else {
		return ascii >= 32;		/* full */
	}
	objc_edit(tree, p->obj, 0, idx, ED_END);
	pw_update_text(p);
	draw_obj(p->obj);
	objc_edit(tree, p->obj, 0, idx, ED_INIT);
	return 1;
}

static short first_editable(void)
{
	short i;
	for (i = 1; i < nobj; i++)
		if (tree[i].ob_flags & EDITABLE)
			return i;
	return 0;
}

/* form_do, with password fields */
static short form_loop(short start)
{
	short next = start ? start : first_editable(), edit = 0, idx = 0, cont = 1;
	short m[8];
	EVENT e;
	while (cont) {
		if (next && next != edit) {
			edit = next;
			next = 0;
			objc_edit(tree, edit, 0, &idx, ED_INIT);
		}
		evnt_multi_(MU_KEYBD | MU_BUTTON, 2, 1, 1, 0, m, &e);
		if (e.which & MU_KEYBD) {
			PWFIELD *p = pw_of(edit);
			short scan = KEY_SCAN(e.kreturn);
			short nav = scan == 0x1c || scan == 0x72 || scan == 0x0f || scan == 0x48 || scan == 0x50;
			if (!(p && !nav && pw_key(p, e.kreturn, &idx))) {
				short kr;
				cont = form_keybd(tree, edit, next, e.kreturn, &next, &kr);
				if (kr)
					objc_edit(tree, edit, kr, &idx, ED_CHAR);
			}
		}
		if (e.which & MU_BUTTON) {
			short obj = objc_find(tree, 0, 8, e.mx, e.my), i;
			PWFIELD *eye = 0;
			for (i = 0; i < npw; i++)
				if (pwf[i].eye == obj && obj > 0)
					eye = &pwf[i];
			if (eye) {
				/* show or hide the password; the dialog stays */
				if (edit)
					objc_edit(tree, edit, 0, &idx, ED_END);
				eye->clear = !eye->clear;
				pw_update_text(eye);
				draw_obj(eye->obj);
				draw_obj(eye->eye);
				if (edit)
					objc_edit(tree, edit, 0, &idx, ED_INIT);
				evnt_timer_(150);
			} else if (obj < 0) {
				next = 0;
			} else {
				cont = form_button(tree, obj, e.breturn, &next);
			}
		}
		if (!cont || (next && next != edit))
			if (edit)
				objc_edit(tree, edit, 0, &idx, ED_END);
	}
	return next;
}

static short d_do(short edit)
{
	short x, y, w, h, r;
	form_center(tree, &x, &y, &w, &h);
	wind_update(BEG_UPDATE);
	wind_update(3);			/* BEG_MCTRL: the dialog owns the mouse */
	form_dial(FMD_START, x, y, w, h);
	objc_draw(tree, 0, 8, x, y, w, h);
	r = (npw ? form_loop(edit) : form_do(tree, edit)) & 0x7fff;
	form_dial(FMD_FINISH, x, y, w, h);
	wind_update(2);
	wind_update(END_UPDATE);
	tree[r].ob_state &= ~SELECTED;
	npw = 0;
	return r;
}

static int selected(short i)
{
	return (tree[i].ob_state & SELECTED) != 0;
}

/* ---------------- alerts ---------------- */

/* format, then make the message part fit: no brackets, lines of at most
   40 characters, at most 5 lines */
short alert(short def, const char *fmt, ...)
{
	char raw[600], out[600];
	const char *msg, *btn;
	va_list ap;
	short o = 0, col = 0, lines = 1;
	va_start(ap, fmt);
	vsnprintf(raw, sizeof(raw), fmt, ap);
	va_end(ap);
	msg = strstr(raw, "][");
	btn = 0;
	{
		const char *p;
		for (p = raw; (p = strstr(p, "][")); p++)
			btn = p;
	}
	if (!msg || !btn || btn == msg)
		return form_alert(def, raw);
	msg += 2;
	memcpy(out, raw, msg - raw);
	o = msg - raw;
	while (msg < btn && o < (short)sizeof(out) - 40) {
		char c = *msg++;
		if (c == '[')
			c = '(';
		else if (c == ']')
			c = ')';
		if (c == '|') {
			if (lines >= 5)
				break;
			lines++;
			col = 0;
			out[o++] = c;
			continue;
		}
		if (col >= 38 && c == ' ' && lines < 5) {
			out[o++] = '|';
			lines++;
			col = 0;
			continue;
		}
		if (col >= 44) {
			if (lines >= 5)
				continue;
			out[o++] = '|';
			lines++;
			col = 0;
		}
		out[o++] = c;
		col++;
	}
	str_copy(out + o, btn, sizeof(out) - o);
	return form_alert(def, out);
}


/* ---------------- account ---------------- */

static void sig_to_line(char *d, const char *s, int size)
{
	int o = 0;
	for (; *s && o < size - 1; s++)
		d[o++] = *s == '\n' ? '|' : *s;
	d[o] = 0;
}

static void line_to_sig(char *d, const char *s, int size)
{
	int o = 0;
	for (; *s && o < size - 1; s++)
		d[o++] = *s == '|' ? '\n' : *s;
	d[o] = 0;
}

int dlg_account(ACCOUNT *a)
{
	static char name[32], full[32], email[44], host[40], port[7], user[44], pass[32],
		    shost[40], sport[7], suser[44], spass[32], sig[50];
	short f_name, f_full, f_email, f_imap, f_pop, f_host, f_port, f_user, f_pass, f_leave,
	      f_shost, f_sport, f_suser, f_spass, f_sig, b_ok, b_del, r;

	str_copy(name, a->name, sizeof(name));
	str_copy(full, a->fullname, sizeof(full));
	str_copy(email, a->email, sizeof(email));
	str_copy(host, a->host, sizeof(host));
	snprintf(port, sizeof(port), "%u", a->port);
	str_copy(user, a->user, sizeof(user));
	str_copy(pass, a->pass, sizeof(pass));
	str_copy(shost, a->smtphost, sizeof(shost));
	snprintf(sport, sizeof(sport), "%u", a->smtpport);
	str_copy(suser, a->smtpuser, sizeof(suser));
	str_copy(spass, a->smtppass, sizeof(spass));
	sig_to_line(sig, a->signature, sizeof(sig));

	d_begin(62, 23);
	d_add(G_STRING, 0, 0, (long)"Mail account", 2, 1, 12, 1);
	d_text(2, 3, "Account name:");
	f_name = d_edit(17, 3, name, 30, 'X');
	d_text(2, 4, "Your name:");
	f_full = d_edit(17, 4, full, 30, 'X');
	d_text(2, 5, "E-mail:");
	f_email = d_edit(17, 5, email, 42, 'X');

	d_text(2, 7, "Incoming mail:");
	f_imap = d_add(G_BUTTON, SELECTABLE | RBUTTON, a->pop ? 0 : SELECTED, (long)"IMAP", 17, 7, 8, 1);
	f_pop = d_add(G_BUTTON, SELECTABLE | RBUTTON, a->pop ? SELECTED : 0, (long)"POP3", 27, 7, 8, 1);
	d_text(2, 8, "Server:");
	f_host = d_edit(17, 8, host, 30, 'X');
	d_text(49, 8, "Port:");
	f_port = d_edit(55, 8, port, 5, '9');
	d_text(2, 9, "User:");
	f_user = d_edit(17, 9, user, 42, 'X');
	d_text(2, 10, "Password:");
	f_pass = d_pass(17, 10, pass, 30);
	f_leave = d_check(17, 11, "POP3: leave mail on the server", a->leave);

	d_text(2, 13, "Outgoing (SMTP)");
	d_text(2, 14, "Server:");
	f_shost = d_edit(17, 14, shost, 30, 'X');
	d_text(49, 14, "Port:");
	f_sport = d_edit(55, 14, sport, 5, '9');
	d_text(2, 15, "User:");
	f_suser = d_edit(17, 15, suser, 42, 'X');
	d_text(2, 16, "Password:");
	f_spass = d_pass(17, 16, spass, 30);
	d_text(17, 17, "(empty: as incoming, \"-\": no login)");
	d_text(2, 18, "Signature:");
	f_sig = d_edit(13, 18, sig, 46, 'X');
	d_text(13, 19, "(\"|\" starts a new line)");

	b_del = d_button(2, 21, 10, "Delete", EXIT);
	d_button(38, 21, 10, "Cancel", EXIT);
	b_ok = d_button(50, 21, 10, "OK", EXIT | DEFAULT);
	d_end();
	(void)f_name; (void)f_full; (void)f_email; (void)f_host; (void)f_port; (void)f_user;
	(void)f_pass; (void)f_shost; (void)f_sport; (void)f_suser; (void)f_spass; (void)f_sig; (void)f_imap;

	r = d_do(f_name);
	if (r == b_del) {
		if (alert(2, "[2][Delete the account|\"%s\"?|(Its mail stays on the server.)][Delete|Cancel]", a->name) == 1)
			return -1;
		return 0;
	}
	if (r != b_ok)
		return 0;
	str_copy(a->name, name[0] ? name : "Mail", sizeof(a->name));
	str_copy(a->fullname, full, sizeof(a->fullname));
	str_copy(a->email, email, sizeof(a->email));
	a->pop = selected(f_pop);
	str_copy(a->host, host, sizeof(a->host));
	a->port = (unsigned short)atoi(port);
	if (!a->port)
		a->port = a->pop ? 110 : 143;
	str_copy(a->user, user, sizeof(a->user));
	str_copy(a->pass, pass, sizeof(a->pass));
	a->leave = selected(f_leave);
	str_copy(a->smtphost, shost, sizeof(a->smtphost));
	a->smtpport = (unsigned short)atoi(sport);
	if (!a->smtpport)
		a->smtpport = 587;
	str_copy(a->smtpuser, suser, sizeof(a->smtpuser));
	str_copy(a->smtppass, spass, sizeof(a->smtppass));
	line_to_sig(a->signature, sig, sizeof(a->signature));
	return 1;
}

/* ---------------- settings ---------------- */

int dlg_settings(void)
{
	static char tz[6], check[4], hdrs[5], wrap[3];
	short f_tz, f_check, f_hdrs, f_wrap, f_keep, f_log, f_heb, b_ok, r;
	snprintf(tz, sizeof(tz), "%d", opt.tz);
	snprintf(check, sizeof(check), "%d", opt.check);
	snprintf(hdrs, sizeof(hdrs), "%d", opt.page);
	snprintf(wrap, sizeof(wrap), "%d", opt.wrap);

	d_begin(52, 15);
	d_add(G_STRING, 0, 0, (long)"Settings", 2, 1, 8, 1);
	d_text(2, 3, "Time zone, minutes east of UTC:");
	f_tz = d_edit(40, 3, tz, 5, 'X');
	d_text(2, 4, "Check mail every N minutes (0: off):");
	f_check = d_edit(40, 4, check, 3, '9');
	d_text(2, 5, "Messages to load at a time:");
	f_hdrs = d_edit(40, 5, hdrs, 4, '9');
	d_text(2, 6, "Wrap my lines at column:");
	f_wrap = d_edit(40, 6, wrap, 2, '9');
	f_keep = d_check(2, 8, "Keep read messages on disk", opt.keepcache);
	f_log = d_check(2, 9, "Write a protocol log (MAIL.LOG)", opt.log);
	f_heb = d_check(2, 10, "Start with the Hebrew keyboard", opt.hebrew);
	d_button(28, 13, 10, "Cancel", EXIT);
	b_ok = d_button(40, 13, 10, "OK", EXIT | DEFAULT);
	d_end();
	(void)f_check; (void)f_hdrs; (void)f_wrap;
	r = d_do(f_tz);
	if (r != b_ok)
		return 0;
	opt.tz = (short)atoi(tz);
	opt.check = (short)atoi(check);
	opt.page = (short)atoi(hdrs);
	if (opt.page < 20)
		opt.page = 20;
	if (opt.page > 1000)
		opt.page = 1000;
	opt.wrap = (short)atoi(wrap);
	if (opt.wrap < 40 || opt.wrap > 78)
		opt.wrap = 72;
	opt.keepcache = selected(f_keep);
	opt.log = selected(f_log);
	opt.hebrew = selected(f_heb);
	return 1;
}

/* ---------------- a line of text ---------------- */

int dlg_ask(const char *title, const char *label, char *buf, short len)
{
	static char field[48];
	short f, b_ok;
	if (len > 46)
		len = 46;
	str_copy(field, buf, sizeof(field));
	d_begin(len + 6 > 40 ? len + 6 : 40, 8);
	d_add(G_STRING, 0, 0, (long)title, 2, 1, (short)strlen(title), 1);
	d_text(2, 3, label);
	f = d_edit(2, 4, field, len, 'X');
	d_button((len + 6 > 40 ? len + 6 : 40) - 24, 6, 10, "Cancel", EXIT);
	b_ok = d_button((len + 6 > 40 ? len + 6 : 40) - 12, 6, 10, "OK", EXIT | DEFAULT);
	d_end();
	if (d_do(f) != b_ok || !field[0])
		return 0;
	str_copy(buf, field, len + 1);
	return 1;
}

/* ---------------- pickers ---------------- */

#define PAGE 14

/* choose one of n strings (Atari charset); -1 = cancelled */
static short pick(const char *title, const char **items, short n)
{
	static char shown[PAGE][40];
	short first = 0;
	for (;;) {
		short i, obj[PAGE], b_prev = -1, b_next = -1, b_cancel, r, k = 0;
		short rows = n - first < PAGE ? n - first : PAGE;
		d_begin(40, rows + 6);
		d_add(G_STRING, 0, 0, (long)title, 2, 1, (short)strlen(title), 1);
		for (i = first; i < n && k < PAGE; i++, k++) {
			short len = (short)strlen(items[i]);
			if (len > 34)
				len = 34;
			/* GEM draws in storage order: lay Hebrew out first */
			if (bidi_has_rtl(items[i], len))
				bidi_visual(items[i], len, bidi_is_rtl(items[i], len), shown[k]);
			else
				memcpy(shown[k], items[i], len);
			shown[k][len] = 0;
			obj[k] = d_add(G_BUTTON, SELECTABLE | EXIT, 0, (long)shown[k], 3, 3 + k, 34, 1);
		}
		if (first > 0)
			b_prev = d_button(3, rows + 4, 8, "<<", EXIT);
		if (first + PAGE < n)
			b_next = d_button(13, rows + 4, 8, ">>", EXIT);
		b_cancel = d_button(27, rows + 4, 10, "Cancel", EXIT | DEFAULT);
		d_end();
		r = d_do(0);
		if (r == b_cancel)
			return -1;
		if (r == b_prev) {
			first -= PAGE;
			continue;
		}
		if (r == b_next) {
			first += PAGE;
			continue;
		}
		for (i = 0; i < k; i++)
			if (obj[i] == r)
				return first + i;
		return -1;
	}
}

short dlg_pick_list(const char *title, const char **items, short n);
short dlg_pick_list(const char *title, const char **items, short n)
{
	return pick(title, items, n);
}

FINFO *dlg_pick_folder(ACCOUNT *a, const char *title)
{
	const char *items[MAXFOLDER];
	FINFO *map[MAXFOLDER];
	short i, n = 0, r;
	for (i = 0; i < a->nfolders; i++) {
		FINFO *f = &a->folders[i];
		if (f->noselect || f->role == FR_OUTBOX || f == cur_finfo)
			continue;
		if (cur_finfo && f->local != cur_finfo->local)
			continue;
		items[n] = f->disp;
		map[n++] = f;
	}
	if (!n) {
		alert(1, "[1][There is no other folder|to choose.][ OK ]");
		return 0;
	}
	r = pick(title, items, n);
	return r < 0 ? 0 : map[r];
}

ACCOUNT *dlg_pick_account(const char *title)
{
	const char *items[MAXACCT];
	short i, r;
	if (naccts == 1)
		return accts[0];
	for (i = 0; i < naccts; i++)
		items[i] = accts[i]->name;
	r = pick(title, items, naccts);
	return r < 0 ? 0 : accts[r];
}

/* the address book: returns a malloc'ed "Name <a@b>" or NULL */
char *dlg_pick_address(void);
char *dlg_pick_address(void)
{
	long len;
	char *book = abook_load(&len), *p, *res = 0;
	const char *items[200];
	short n = 0, r;
	if (!book || !len) {
		free(book);
		alert(1, "[1][The address book is empty.|Addresses you write to are|added to it.][ OK ]");
		return 0;
	}
	for (p = book; *p && n < 200; ) {
		char *e = strchr(p, '\n');
		if (e)
			*e = 0;
		if (*p && p[strlen(p) - 1] == '\r')
			p[strlen(p) - 1] = 0;
		if (*p)
			items[n++] = p;
		if (!e)
			break;
		p = e + 1;
	}
	r = pick("Address book", items, n);
	if (r >= 0)
		res = strdup(items[r]);
	free(book);
	return res;
}
