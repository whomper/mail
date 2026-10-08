/*
 * editor.c - the compose window: a plain-text editor holding the
 * header lines (To, Cc, Bcc, Subject, Attach) above the body. Lines are
 * kept in logical order; Hebrew lines are shown right to left with the
 * cursor at the right visual place, and F10 switches the keyboard to
 * the Hebrew layout. Body lines wrap at the configured column.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "../src/mail.h"
#include "../src/compose.h"
#include "../src/bidi.h"
#include "../src/util.h"

typedef struct {
	char *s;
	short n, cap;
} ELINE;

#define MAXLINES 4000

static ELINE *ln;
static long nl;
static long cy;
static short cx;
static short dirty;
static ACCOUNT *e_acct;
static char *e_irt, *e_refs;
static unsigned long e_reply_uid;
static FINFO *e_reply_fi;

int editor_dirty(void)
{
	return w_editor.h > 0 && dirty;
}

static int grow_line(ELINE *l, short need)
{
	if (need + 1 <= l->cap)
		return 1;
	{
		short cap = l->cap ? l->cap : 16;
		char *n;
		while (cap < need + 1)
			cap *= 2;
		n = realloc(l->s, cap);
		if (!n)
			return 0;
		l->s = n;
		l->cap = cap;
	}
	return 1;
}

static int ins_line(long at, const char *s, short n)
{
	ELINE *l;
	if (nl >= MAXLINES)
		return 0;
	memmove(ln + at + 1, ln + at, (nl - at) * sizeof(ELINE));
	nl++;
	l = &ln[at];
	memset(l, 0, sizeof(*l));
	if (!grow_line(l, n))
		return 0;
	memcpy(l->s, s, n);
	l->n = n;
	l->s[n] = 0;
	return 1;
}

static void del_line(long at)
{
	free(ln[at].s);
	memmove(ln + at, ln + at + 1, (nl - at - 1) * sizeof(ELINE));
	nl--;
}

static void free_all(void)
{
	long i;
	for (i = 0; i < nl; i++)
		free(ln[i].s);
	nl = 0;
	free(e_irt);
	free(e_refs);
	e_irt = e_refs = 0;
}

/* index of the blank line that ends the header lines, or -1 */
static long header_end(void)
{
	long i;
	for (i = 0; i < nl; i++)
		if (ln[i].n == 0)
			return i;
	return -1;
}

static int in_body(long line)
{
	long he = header_end();
	return he >= 0 && line > he;
}

static void set_title(void)
{
	char t[100];
	snprintf(t, sizeof(t), " %s - %s%s ", "New message", e_acct ? e_acct->name : "", dirty ? " *" : "");
	win_title(&w_editor, t);
	win_info(&w_editor, hebrew_kbd ? " Hebrew keyboard (F10)   ^S send   Esc close"
				       : " ^S send   ^T attach file   F10 Hebrew   Esc close");
}

void editor_open(ACCOUNT *a, char *text, const char *irt, const char *refs)
{
	const char *p;
	if (w_editor.h > 0 && dirty) {
		if (alert(2, "[2][You are still writing a message.|Discard it?][Discard|Cancel]") != 1) {
			free(text);
			win_top(&w_editor);
			return;
		}
	}
	free_all();
	if (!ln)
		ln = calloc(MAXLINES, sizeof(ELINE));
	if (!ln) {
		free(text);
		return;
	}
	e_acct = a;
	e_irt = irt && *irt ? strdup(irt) : 0;
	e_refs = refs && *refs ? strdup(refs) : 0;
	e_reply_uid = irt && *irt ? cur_uid : 0;
	e_reply_fi = cur_finfo;
	for (p = text; ; ) {
		const char *e = strchr(p, '\n');
		short n = (short)(e ? e - p : (long)strlen(p));
		ins_line(nl, p, n);
		if (!e)
			break;
		p = e + 1;
	}
	free(text);
	/* cursor: on an empty To: line, else at the start of the text */
	cy = 0;
	cx = 0;
	if (nl && ln[0].n > 4) {
		long he = header_end();
		cy = he >= 0 && he + 1 < nl ? he + 1 : 0;
	} else if (nl) {
		cx = ln[0].n;
	}
	dirty = 0;
	w_editor.top = 0;
	w_editor.total = nl;
	set_title();
	win_open(&w_editor);
	win_sliders(&w_editor);
	win_redraw(&w_editor, 0);
}

/* visual column of logical column col within s[0..n), laid out the way
   text_at() does it in a field of `cols` (right-aligned when it is a
   Hebrew paragraph and `right` is set) */
static short visual_col(const char *s, short n, short col, short cols, short right)
{
	char vis[BIDI_MAX];
	short pos[BIDI_MAX];
	unsigned char odd[BIDI_MAX];
	short m = n > cols ? cols : n, rtl, xoff;
	if (m > BIDI_MAX)
		m = BIDI_MAX;
	if (m <= 0 || !bidi_has_rtl(s, m))
		return col;
	rtl = bidi_is_rtl(s, m);
	xoff = (rtl && right) ? cols - m : 0;
	bidi_visual_map(s, m, rtl, vis, pos, odd);
	if (col < m)
		return xoff + pos[col];
	return rtl ? (right ? xoff - 1 : -1) : m;
}

/* where the cursor sits on screen: column relative to the work area */
static short cursor_col(long line, short col, short cols, short *hoff)
{
	ELINE *l = &ln[line];
	long he = header_end();
	short off = 0;
	if (col > cols - 2)
		off = col - cols + 2;
	*hoff = off;
	if ((he < 0 || line < he) && !off) {
		/* header line: "Label:" then the value one column further */
		const char *colon = memchr(l->s, ':', l->n);
		if (colon) {
			short lab = (short)(colon - l->s + 1), v = lab;
			if (v < l->n && l->s[v] == ' ')
				v++;
			if (col < v)
				return col;
			return lab + 1 + visual_col(l->s + v, l->n - v, col - v, cols - lab - 1, 0);
		}
	}
	return visual_col(l->s + off, l->n - off, col - off, cols, 1);
}

static void draw(WIN *w, GRECT *clip)
{
	long i, rows = win_rows(w) + 1, he = header_end();
	short cols = w->work.w / cw - 1, x0 = w->work.x + cw / 2;
	fill(clip, 0);
	for (i = w->top; i < nl && i < w->top + rows; i++) {
		ELINE *l = &ln[i];
		short y = w->work.y + (short)((i - w->top) * ch), hoff = 0;
		if (i == cy && cx > cols - 2)
			hoff = cx - cols + 2;
		if (he < 0 || i < he) {
			const char *colon = memchr(l->s, ':', l->n);
			if (colon && !hoff) {
				/* the value one column after "Label:", whatever its
				   direction */
				short lab = (short)(colon - l->s + 1), v = lab;
				text_at(x0, y, l->s, lab, cols, TX_BOLD);
				if (v < l->n && l->s[v] == ' ')
					v++;
				text_at(x0 + (lab + 1) * cw, y, l->s + v, l->n - v, cols - lab - 1, 0);
			} else {
				text_at(x0, y, l->s + hoff, l->n - hoff, cols, 0);
			}
		} else if (i == he) {
			hline(w->work.x, w->work.x + w->work.w - 1, y + ch / 2);
		} else {
			text_at(x0, y, l->s + hoff, l->n - hoff, cols,
				TX_RIGHT | ((l->n && l->s[0] == '>') ? TX_LIGHT : 0));
		}
		if (i == cy && w->h > 0 && win_is_top(w)) {
			short c = cursor_col(i, cx, cols, &hoff), pxy[4];
			if (c < 0)
				c = 0;
			pxy[0] = x0 + c * cw;
			pxy[1] = y;
			pxy[2] = pxy[0] + 1;
			pxy[3] = y + ch - 1;
			vswr_mode(vdi_h, 3);	/* XOR */
			vsf_interior(vdi_h, 1);
			vsf_color(vdi_h, 1);
			vr_recfl(vdi_h, pxy);
			vswr_mode(vdi_h, 1);
		}
	}
}

static void changed(long from, long n)
{
	if (!dirty) {
		dirty = 1;
		set_title();
	}
	w_editor.total = nl;
	win_sliders(&w_editor);
	if (n < 0)
		win_redraw_lines(&w_editor, from, win_rows(&w_editor) + 1);
	else
		win_redraw_lines(&w_editor, from, n);
}

static void move_to(long line, short col)
{
	long old = cy;
	if (line < 0)
		line = 0;
	if (line >= nl)
		line = nl - 1;
	if (col < 0)
		col = 0;
	if (col > ln[line].n)
		col = ln[line].n;
	cy = line;
	cx = col;
	win_redraw_lines(&w_editor, old, 1);
	win_ensure_visible(&w_editor, cy);
	win_redraw_lines(&w_editor, cy, 1);
}

/* wrap the current body line at opt.wrap, moving the cursor along */
static void wrap_current(void)
{
	ELINE *l = &ln[cy];
	short sp, keep;
	if (!in_body(cy) || l->n <= opt.wrap || (l->n && l->s[0] == '>'))
		return;
	for (sp = opt.wrap; sp > 0 && l->s[sp] != ' '; sp--)
		;
	if (sp <= 0)
		return;
	keep = sp;
	if (!ins_line(cy + 1, l->s + sp + 1, l->n - sp - 1))
		return;
	l = &ln[cy];
	l->n = keep;
	l->s[keep] = 0;
	if (cx > keep) {
		cx -= keep + 1;
		cy++;
	}
	changed(cy - 1, -1);
}

static void insert_char(char c)
{
	ELINE *l = &ln[cy];
	if (!grow_line(l, l->n + 1))
		return;
	memmove(l->s + cx + 1, l->s + cx, l->n - cx + 1);
	l->s[cx++] = c;
	l->n++;
	if (in_body(cy) && l->n > opt.wrap && c != ' ')
		wrap_current();
	changed(cy, 1);
	win_ensure_visible(&w_editor, cy);
}

static void split_line(void)
{
	ELINE *l = &ln[cy];
	if (!ins_line(cy + 1, l->s + cx, l->n - cx))
		return;
	l = &ln[cy];
	l->n = cx;
	l->s[cx] = 0;
	cy++;
	cx = 0;
	changed(cy - 1, -1);
	win_ensure_visible(&w_editor, cy);
}

static void join_next(long line)
{
	ELINE *a = &ln[line], *b;
	if (line + 1 >= nl)
		return;
	b = &ln[line + 1];
	if (!grow_line(a, a->n + b->n))
		return;
	memcpy(a->s + a->n, b->s, b->n + 1);
	a->n += b->n;
	del_line(line + 1);
	changed(line, -1);
}

static int key(WIN *w, short kstate, short k)
{
	short scan = KEY_SCAN(k);
	long rows = win_rows(w);
	short rtl = line_rtl(ln[cy].s, ln[cy].n);
	unsigned char c;

	switch (scan) {
	case 0x1c:	/* Return */
	case 0x72:
		split_line();
		return 1;
	case 0x0e:	/* Backspace */
		if (cx > 0) {
			ELINE *l = &ln[cy];
			memmove(l->s + cx - 1, l->s + cx, l->n - cx + 1);
			l->n--;
			cx--;
			changed(cy, 1);
		} else if (cy > 0) {
			short n = ln[cy - 1].n;
			cy--;
			join_next(cy);
			cx = n;
			win_ensure_visible(w, cy);
		}
		return 1;
	case 0x53:	/* Delete */
		if (cx < ln[cy].n) {
			ELINE *l = &ln[cy];
			memmove(l->s + cx, l->s + cx + 1, l->n - cx);
			l->n--;
			changed(cy, 1);
		} else {
			join_next(cy);
		}
		return 1;
	case 0x4b:	/* left: backwards, or forwards in a Hebrew line */
	case 0x4d: {
		short fwd = (scan == 0x4d) ^ rtl;
		if (kstate & 3) {
			move_to(cy, fwd ? ln[cy].n : 0);
		} else if (fwd) {
			if (cx < ln[cy].n)
				move_to(cy, cx + 1);
			else if (cy + 1 < nl)
				move_to(cy + 1, 0);
		} else {
			if (cx > 0)
				move_to(cy, cx - 1);
			else if (cy > 0)
				move_to(cy - 1, ln[cy - 1].n);
		}
		return 1;
	}
	case 0x48:
		move_to(cy - ((kstate & 3) ? rows : 1), cx);
		return 1;
	case 0x50:
		move_to(cy + ((kstate & 3) ? rows : 1), cx);
		return 1;
	case 0x47:	/* Clr/Home */
		if (kstate & 3)
			move_to(nl - 1, ln[nl - 1].n);
		else
			move_to(cy, 0);
		return 1;
	case 0x0f:	/* Tab: next header line, or spaces in the text */
		if (!in_body(cy)) {
			long he = header_end();
			if (cy + 1 < (he >= 0 ? he : nl))
				move_to(cy + 1, ln[cy + 1].n);
			else
				move_to(he >= 0 && he + 1 < nl ? he + 1 : cy, 0);
		} else {
			do
				insert_char(' ');
			while (cx % 4);
		}
		return 1;
	case 0x01:	/* Esc */
		w->closed(w);
		return 1;
	}
	if (kstate & K_CTRL)
		return 0;
	c = key_char(kstate, k);
	if (c >= 32 || c >= 0x80) {
		insert_char((char)c);
		return 1;
	}
	return 0;
}

static char *join_text(long *len)
{
	SBUF b;
	long i;
	sb_init(&b);
	for (i = 0; i < nl; i++) {
		sb_add(&b, ln[i].s, ln[i].n);
		if (i + 1 < nl)
			sb_addc(&b, '\n');
	}
	*len = b.len;
	return sb_steal(&b);
}

void editor_send(int now)
{
	long len, rl;
	char *text, *raw, err[200];
	int sent = 0, r;
	if (w_editor.h <= 0 || !e_acct)
		return;
	text = join_text(&len);
	raw = compose_build(e_acct, text, len, e_irt, e_refs, err, sizeof(err), &rl);
	free(text);
	if (!raw) {
		alert(1, "[1][%s][ OK ]", err);
		return;
	}
	if (!mail_queue(e_acct, raw, rl)) {
		alert(1, "[1][%s][ OK ]", mail_err);
		free(raw);
		return;
	}
	free(raw);
	/* remember who we write to */
	{
		long i, he = header_end();
		for (i = 0; i < (he >= 0 ? he : nl); i++) {
			if (!strncasecmp(ln[i].s, "To:", 3) || !strncasecmp(ln[i].s, "Cc:", 3)) {
				char list[20][160];
				int n = addr_list(ln[i].s + 3, list, 20), j;
				for (j = 0; j < n; j++)
					abook_add(list[j]);
			}
		}
	}
	/* mark the original answered */
	if (e_reply_uid && cur_folder && cur_finfo == e_reply_fi) {
		HDR *h = fold_get(cur_folder, e_reply_uid);
		if (h && !opt.offline)
			mail_flag(cur_folder, h, MF_ANSWERED, 1);
	}
	dirty = 0;
	win_close(&w_editor);
	free_all();
	if (now && !opt.offline) {
		busy(1);
		mail_err[0] = 0;
		r = mail_send_outbox(e_acct, &sent);
		busy(0);
		if (r < 0)
			alert(1, "[1][%s||The message waits in the Outbox.][ OK ]", mail_err);
	} else {
		alert(1, "[1][The message is in the Outbox.|It goes out with the next|Check mail.][ OK ]");
	}
	folders_build();
	if (cur_finfo && (cur_finfo->role == FR_OUTBOX || cur_finfo->role == FR_SENT)) {
		if (cur_folder)
			fold_close(cur_folder);
		cur_folder = fold_open(cur_acct, cur_finfo);
		list_refresh();
	}
	menu_update();
}

void editor_attach(void)
{
	static char path[160], name[16];
	short button;
	char full[200], line[220];
	long he;
	if (w_editor.h <= 0)
		return;
	if (!path[0]) {
		str_copy(path, opt.workdir, sizeof(path) - 5);
		strcat(path, "\\*.*");
	}
	name[0] = 0;
	if (!fsel_exinput(path, name, &button, "Attach which file?") || !button || !name[0])
		return;
	str_copy(full, path, sizeof(full));
	{
		char *bs = strrchr(full, '\\');
		if (bs)
			bs[1] = 0;
	}
	strncat(full, name, sizeof(full) - strlen(full) - 1);
	snprintf(line, sizeof(line), "Attach: %s", full);
	he = header_end();
	if (he < 0)
		he = nl;
	ins_line(he, line, (short)strlen(line));
	if (cy >= he)
		cy++;
	changed(he, -1);
}

/* add an address book entry to the To: line (or the header line the
   cursor is on, when that is Cc or Bcc) */
void editor_insert_address(void)
{
	char *addr;
	long line = 0, he;
	ELINE *l;
	if (w_editor.h <= 0)
		return;
	addr = dlg_pick_address();
	if (!addr)
		return;
	he = header_end();
	if (cy < (he >= 0 ? he : nl) && (!strncasecmp(ln[cy].s, "Cc:", 3) || !strncasecmp(ln[cy].s, "Bcc:", 4)))
		line = cy;
	l = &ln[line];
	if (grow_line(l, l->n + (short)strlen(addr) + 3)) {
		char *t = str_trim(l->s + (strchr(l->s, ':') ? strchr(l->s, ':') - l->s + 1 : 0));
		if (*t)
			strcat(l->s, ", ");
		else if (l->n && l->s[l->n - 1] != ' ')
			strcat(l->s, " ");
		strcat(l->s, addr);
		l->n = (short)strlen(l->s);
	}
	free(addr);
	changed(line, 1);
}

static void closed(WIN *w)
{
	if (dirty) {
		short b = alert(1, "[2][Keep this message?][Outbox|Discard|Cancel]");
		if (b == 3)
			return;
		if (b == 1) {
			editor_send(0);
			return;
		}
	}
	dirty = 0;
	win_close(w);
	free_all();
}

static void resized(WIN *w)
{
	(void)w;
}

/* clicking puts the cursor there */
static void click(WIN *w, short mx, short my, short clicks, short kstate)
{
	long line = w->top + (my - w->work.y) / ch;
	short col = (mx - w->work.x - cw / 2) / cw, cols = w->work.w / cw - 1;
	(void)clicks;
	(void)kstate;
	if (line >= nl)
		line = nl - 1;
	if (line < 0)
		return;
	if (line_rtl(ln[line].s, ln[line].n)) {
		/* map the visual column back to a logical one */
		char vis[BIDI_MAX];
		short pos[BIDI_MAX], m = ln[line].n > cols ? cols : ln[line].n, i, best = m;
		unsigned char odd[BIDI_MAX];
		short xoff = cols - m;
		bidi_visual_map(ln[line].s, m, 1, vis, pos, odd);
		for (i = 0; i < m; i++)
			if (pos[i] + xoff == col)
				best = i;
		col = best;
	}
	move_to(line, col);
}

void editor_init(void);
void editor_init(void)
{
	w_editor.h = -1;
	w_editor.draw = draw;
	w_editor.key = key;
	w_editor.click = click;
	w_editor.closed = closed;
	w_editor.resized = resized;
	strcpy(w_editor.title, " New message ");
}

