/*
 * dialogs.c - EMail's dialogs, built in code (no .RSC file needed):
 * account setup, settings, a one-line question, list pickers, alerts.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "ui.h"
#include "../src/bidi.h"
#include "../src/util.h"
#include "../src/charset.h"

#define DMAX 72
#define TMAX 24

static OBJECT tree[DMAX];
static void make_eyes(void);
static short npw;
static TEDINFO ted[TMAX];
static char tmpl[TMAX][64], valid[TMAX][64];
static short nobj, nted, last_child;

/* ---------------- builder ---------------- */

OBJECT *d_tree(void)
{
	return tree;
}

void d_begin(short w, short h)
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

short d_add(short type, short flags, short state, long spec, short x, short y, short w, short h)
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

short d_text(short x, short y, const char *s)
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

short d_button(short x, short y, short w, const char *s, short flags)
{
	return d_add(G_BUTTON, SELECTABLE | flags, 0, (long)s, x, y, w, 1);
}

short d_check(short x, short y, const char *s, short on)
{
	return d_add(G_BUTTON, SELECTABLE, on ? SELECTED : 0, (long)s, x, y, (short)strlen(s) + 2, 1);
}

void d_end(void)
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

/* format, then make the message part fit: no brackets, words wrapped
   into lines of at most 30 characters (what every TOS shows), at most 5 */
#define ALW 30

short alert(short def, const char *fmt, ...)
{
	char raw[600], out[600];
	const char *msg, *btn;
	va_list ap;
	short o, lines = 0;
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
	while (msg < btn && lines < 5) {
		/* one line: up to the next '|', or the last space that fits */
		const char *end = msg, *cut;
		short n;
		while (end < btn && *end != '|')
			end++;
		cut = end;
		if (end - msg > ALW) {
			cut = msg + ALW;
			while (cut > msg && *cut != ' ')
				cut--;
			if (cut == msg)
				cut = msg + ALW;	/* one long word: break it */
		}
		if (lines)
			out[o++] = '|';
		for (n = 0; msg < cut && o < (short)sizeof(out) - 40; n++) {
			char c = *msg++;
			out[o++] = c == '[' ? '(' : c == ']' ? ')' : c;
		}
		lines++;
		while (msg < btn && *msg == ' ')
			msg++;
		if (msg < btn && *msg == '|')
			msg++;
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
		    shost[40], sport[7], suser[44], spass[32], sig[50], sig2[50];
	short f_name, f_full, f_email, f_imap, f_pop, f_host, f_port, f_user, f_pass, f_leave,
	      f_shost, f_sport, f_suser, f_spass, f_sig, f_sig2, b_ok, b_del, r;

	str_copy(name, a->name, sizeof(name));
	str_copy(full, a->fullname, sizeof(full));
	str_copy(email, a->email, sizeof(email));
	if (opt.falcon && !a->dhost[0])
		acct_preset(a);
	str_copy(host, opt.falcon ? a->dhost : a->host, sizeof(host));
	snprintf(port, sizeof(port), "%u", opt.falcon ? acct_port(a, 0) : a->port);
	/* each mode has its own login: Falcon mode's goes to the provider,
	   the gateway's to the Pi (which may want something else) */
	acct_fill_logins(a);
	str_copy(user, opt.falcon ? a->duser : a->user, sizeof(user));
	str_copy(pass, opt.falcon ? a->dpass : a->pass, sizeof(pass));
	str_copy(shost, opt.falcon ? a->dsmtphost : a->smtphost, sizeof(shost));
	snprintf(sport, sizeof(sport), "%u", opt.falcon ? acct_port(a, 1) : a->smtpport);
	str_copy(suser, opt.falcon ? a->dsmtpuser : a->smtpuser, sizeof(suser));
	str_copy(spass, opt.falcon ? a->dsmtppass : a->smtppass, sizeof(spass));
	/* two lines to edit; a third and more stay in line 2 after a "|" */
	{
		const char *nl = strchr(a->signature, '\n');
		long n1 = nl ? nl - a->signature : (long)strlen(a->signature);
		if (n1 > (long)sizeof(sig) - 1)
			n1 = sizeof(sig) - 1;
		memcpy(sig, a->signature, n1);
		sig[n1] = 0;
		sig_to_line(sig2, nl ? nl + 1 : "", sizeof(sig2));
	}

	d_begin(62, 23);
	d_add(G_STRING, 0, 0, (long)"Mail account", 2, 1, 12, 1);
	d_text(16, 1, opt.falcon ? "Falcon mode: the provider's servers, login"
				 : "the Pi gateway's servers and login");
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
	f_sig2 = d_edit(13, 19, sig2, 46, 'X');
	d_text(13, 20, "(two lines; a \"|\" in line 2 starts another)");

	b_del = d_button(2, 21, 10, "Delete", EXIT);
	d_button(38, 21, 10, "Cancel", EXIT);
	b_ok = d_button(50, 21, 10, "OK", EXIT | DEFAULT);
	d_end();
	(void)f_name; (void)f_full; (void)f_email; (void)f_host; (void)f_port; (void)f_user;
	(void)f_pass; (void)f_shost; (void)f_sport; (void)f_suser; (void)f_spass; (void)f_sig; (void)f_sig2;
	(void)f_imap;

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
	if (opt.falcon) {
		str_copy(a->dhost, host, sizeof(a->dhost));
		a->dport = (unsigned short)atoi(port);
		if (!a->dport)
			a->dport = a->pop ? 995 : 993;
	} else {
		str_copy(a->host, host, sizeof(a->host));
		a->port = (unsigned short)atoi(port);
		if (!a->port)
			a->port = a->pop ? 110 : 143;
	}
	if (opt.falcon) {
		str_copy(a->duser, user, sizeof(a->duser));
		str_copy(a->dpass, pass, sizeof(a->dpass));
	} else {
		str_copy(a->user, user, sizeof(a->user));
		str_copy(a->pass, pass, sizeof(a->pass));
	}
	a->leave = selected(f_leave);
	if (opt.falcon) {
		str_copy(a->dsmtphost, shost, sizeof(a->dsmtphost));
		a->dsmtpport = (unsigned short)atoi(sport);
		if (!a->dsmtpport)
			a->dsmtpport = 465;
		/* a new account: the provider's servers from the address */
		if (!a->dhost[0])
			acct_preset(a);
	} else {
		str_copy(a->smtphost, shost, sizeof(a->smtphost));
		a->smtpport = (unsigned short)atoi(sport);
		if (!a->smtpport)
			a->smtpport = 587;
	}
	if (opt.falcon) {
		str_copy(a->dsmtpuser, suser, sizeof(a->dsmtpuser));
		str_copy(a->dsmtppass, spass, sizeof(a->dsmtppass));
	} else {
		str_copy(a->smtpuser, suser, sizeof(a->smtpuser));
		str_copy(a->smtppass, spass, sizeof(a->smtppass));
	}
	acct_fill_logins(a);		/* a new account: the other mode starts the same */
	{
		char both[110];
		snprintf(both, sizeof(both), sig2[0] ? "%s|%s" : "%s", sig, sig2);
		line_to_sig(a->signature, both, sizeof(a->signature));
	}
	return 1;
}

/* ---------------- settings ---------------- */

int dlg_settings(void)
{
	static char tz[6], check[4], hdrs[5], wrap[3];
	short f_tz, f_check, f_hdrs, f_wrap, f_keep, f_log, f_heb, f_bridge, f_falcon, f_dsp, b_ok, r;
	snprintf(tz, sizeof(tz), "%d", opt.tz);
	snprintf(check, sizeof(check), "%d", opt.check);
	snprintf(hdrs, sizeof(hdrs), "%d", opt.page);
	snprintf(wrap, sizeof(wrap), "%d", opt.wrap);

	d_begin(52, 18);
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
	f_log = d_check(2, 9, "Write a protocol log (EMAIL.LOG)", opt.log);
	f_heb = d_check(2, 10, "Start with the Hebrew keyboard", opt.hebrew);
	f_bridge = d_check(2, 11, "Bridge sends Hebrew reversed (Troll bridge)", cs_bridge_visual);
	f_falcon = d_check(2, 13, "Falcon mode: secure (TLS) on this Atari", dlg_falcon);
	d_text(4, 14, "off: plain, through the Raspberry Pi gateway");
	f_dsp = d_check(4, 15, "use the DSP for the signatures", opt.dsp);
	d_button(28, 16, 10, "Cancel", EXIT);
	b_ok = d_button(40, 16, 10, "OK", EXIT | DEFAULT);
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
	cs_bridge_visual = selected(f_bridge);
	dlg_falcon = selected(f_falcon);
	opt.dsp = selected(f_dsp);
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

#define PROWS 12			/* rows shown at a time */
#define PW 38				/* the widest list, in characters */

/* choose one of n strings (Atari charset); -1 = cancelled. One dialog
   that stays open: a framed list with arrows to page through it (the
   list is redrawn in place), a click picks a row, OK or a double click
   takes it. */
static short pick_w(const char *title, const char **items, short n, short lw)
{
	static char shown[PROWS][PW + 2], count[40];
	short first = 0, cur = -1, row[PROWS], box, b_up, b_down, b_ok, b_cancel, f_count;
	short x, y, w, h, bx, by, r, i, result = -1, tw = (short)strlen(title);

	if (lw > PW)
		lw = PW;
	if (lw < 22)
		lw = 22;
	if (tw > lw + 2)
		lw = tw > PW ? PW : tw - 2;
	d_begin(lw + 8, PROWS + 8);
	d_add(G_STRING, 0, 0, (long)title, 2, 1, (short)strlen(title), 1);
	box = d_add(G_BOX, 0, 0, 0x00FF1100L, 2, 3, lw, PROWS);
	for (i = 0; i < PROWS; i++)
		row[i] = d_add(G_STRING, TOUCHEXIT, 0, (long)shown[i], 2, 3 + i, lw, 1);
	b_up = d_add(G_BOXCHAR, TOUCHEXIT, 0, 0x01FF1100L, lw + 3, 3, 2, 1);
	b_down = d_add(G_BOXCHAR, TOUCHEXIT, 0, 0x02FF1100L, lw + 3, 2 + PROWS, 2, 1);
	f_count = d_add(G_STRING, 0, 0, (long)count, 2, PROWS + 4, lw, 1);
	b_cancel = d_button(lw - 18, PROWS + 6, 10, "Cancel", EXIT);
	b_ok = d_button(lw - 6, PROWS + 6, 10, "OK", EXIT | DEFAULT);
	d_end();

	for (;;) {
		/* fill the rows from `first`; the chosen one inverted */
		short k;
		for (k = 0; k < PROWS; k++) {
			short it = first + k, len = 0;
			memset(shown[k], ' ', lw);
			shown[k][lw] = 0;
			tree[row[k]].ob_state = 0;
			if (it < n) {
				const char *s = items[it];
				len = (short)strlen(s);
				if (len > lw - 2)
					len = lw - 2;
				/* GEM draws in storage order: lay Hebrew out first */
				if (bidi_has_rtl(s, len))
					bidi_visual(s, len, bidi_is_rtl(s, len), shown[k] + 1);
				else
					memcpy(shown[k] + 1, s, len);
				heb_font_map(shown[k] + 1, len, opt.hebfont);
				if (it == cur)
					tree[row[k]].ob_state = SELECTED;
			}
		}
		if (n > PROWS)
			snprintf(count, sizeof(count), "%d-%d of %d", first + 1,
				 first + PROWS < n ? first + PROWS : n, n);
		else
			snprintf(count, sizeof(count), n == 1 ? "1 to choose from" : "%d to choose from", n);
		tree[b_up].ob_state = first > 0 ? 0 : DISABLED;
		tree[b_down].ob_state = first + PROWS < n ? 0 : DISABLED;
		tree[b_ok].ob_state = cur >= 0 ? 0 : DISABLED;
		if (result == -1) {
			/* first time round: open the dialog */
			form_center(tree, &x, &y, &w, &h);
			wind_update(BEG_UPDATE);
			wind_update(3);
			form_dial(FMD_START, x, y, w, h);
			objc_draw(tree, 0, 8, x, y, w, h);
			result = -2;
		} else {
			/* only the list, the arrows, the count and OK change */
			objc_offset(tree, box, &bx, &by);
			objc_draw(tree, 0, 8, bx - 1, by - 1, tree[box].ob_width + 2, tree[box].ob_height + 2);
			objc_draw(tree, b_up, 0, x, y, w, h);
			objc_draw(tree, b_down, 0, x, y, w, h);
			objc_offset(tree, f_count, &bx, &by);
			objc_draw(tree, 0, 8, bx, by, tree[f_count].ob_width, tree[f_count].ob_height);
			objc_draw(tree, b_ok, 0, x, y, w, h);
		}
		r = form_do(tree, 0);
		{
			int dbl = (r & 0x8000) != 0;
			r &= 0x7fff;
			tree[r].ob_state &= ~SELECTED;
			if (r == b_cancel)
				break;
			if (r == b_ok) {
				if (cur >= 0) {
					result = cur;
					break;
				}
				continue;
			}
			if (r == b_up && first > 0) {
				first -= PROWS;
				if (first < 0)
					first = 0;
				evnt_timer_(120);	/* held down: a page at a time */
				continue;
			}
			if (r == b_down && first + PROWS < n) {
				first += PROWS;
				evnt_timer_(120);
				continue;
			}
			for (k = 0; k < PROWS; k++)
				if (r == row[k] && first + k < n) {
					cur = first + k;
					if (dbl)
						result = cur;
				}
			if (result >= 0)
				break;
		}
	}
	form_dial(FMD_FINISH, x, y, w, h);
	wind_update(2);
	wind_update(END_UPDATE);
	return result >= 0 ? result : -1;
}

static short pick(const char *title, const char **items, short n)
{
	return pick_w(title, items, n, PW);
}

short dlg_pick_list(const char *title, const char **items, short n);
short dlg_pick_list(const char *title, const char **items, short n)
{
	return pick(title, items, n);
}

/* a folder's name with its parents', "Archive / 2023", so two folders
   of the same name can be told apart */
static void folder_label(ACCOUNT *a, FINFO *f, char *out, int size)
{
	FINFO *parent = 0;
	short i;
	size_t best = 0;
	if (f->depth > 0) {
		for (i = 0; i < a->nfolders; i++) {
			FINFO *g = &a->folders[i];
			size_t l = strlen(g->server);
			if (g != f && g->depth == f->depth - 1 && l > best && !strncmp(g->server, f->server, l) &&
			    f->server[l] && !((f->server[l] | 0x20) >= 'a' && (f->server[l] | 0x20) <= 'z')) {
				parent = g;
				best = l;
			}
		}
	}
	if (parent) {
		char up[120];
		folder_label(a, parent, up, sizeof(up));
		snprintf(out, size, "%s / %s", up, f->disp);
	} else {
		str_copy(out, f->disp, size);
	}
}

static int cmp_label(const void *x, const void *y)
{
	const char *a = *(const char * const *)x, *b = *(const char * const *)y;
	return strcasecmp(a, b);
}

FINFO *dlg_pick_folder(ACCOUNT *a, const char *title)
{
	static char labels[MAXFOLDER][100];
	const char *items[MAXFOLDER];
	FINFO *map[MAXFOLDER];
	short i, n = 0, r;
	for (i = 0; i < a->nfolders; i++) {
		FINFO *f = &a->folders[i];
		if (f->noselect || f->role == FR_OUTBOX || f == cur_finfo)
			continue;
		if (cur_finfo && f->local != cur_finfo->local)
			continue;
		folder_label(a, f, labels[n], sizeof(labels[n]));
		items[n] = labels[n];
		n++;
	}
	if (!n) {
		alert(1, "[1][There is no other folder|to choose.][ OK ]");
		return 0;
	}
	/* alphabetical; the labels sit in a fixed array, so find each one's
	   folder again after sorting */
	qsort(items, n, sizeof(items[0]), cmp_label);
	for (r = 0; r < n; r++) {
		short k = 0;
		for (i = 0; i < a->nfolders; i++) {
			FINFO *f = &a->folders[i];
			if (f->noselect || f->role == FR_OUTBOX || f == cur_finfo ||
			    (cur_finfo && f->local != cur_finfo->local))
				continue;
			if (labels[k] == items[r])
				map[r] = f;
			k++;
		}
	}
	r = pick_w(title, items, n, 25);	/* folder names are short */
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

/* ---------------- the keys (Help) ---------------- */

void dlg_keys(void)
{
	static const char *lines[] = {
		"^N  new message        ^K  check mail",
		"^R  reply              ^E  reply to all",
		"^F  forward            ^U  mark as unread",
		"^G  flag / unflag      ^M  move to a folder",
		"Del delete             ^A  select all",
		"Shift+click   add or remove one message",
		"Control+click select a range of messages",
		"Tab next pane          F10 Hebrew keyboard",
		"In the editor: ^S send  ^T attach  ^B addresses",
		"^Q  quit"
	};
	short i, n = (short)(sizeof(lines) / sizeof(lines[0]));
	d_begin(52, n + 6);
	d_add(G_STRING, 0, 0, (long)"Keys", 2, 1, 4, 1);
	for (i = 0; i < n; i++)
		d_text(3, 3 + i, lines[i]);
	d_button(40, n + 4, 10, "OK", EXIT | DEFAULT);
	d_end();
	d_do(0);
}
