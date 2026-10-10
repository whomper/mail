/*
 * win.c - EMail's windows.
 *
 * The main window holds three panes, like a modern mail program and
 * Claude ST's sidebar: folders on the left, the message list on the
 * right, the message below it. The dividers between them can be dragged,
 * each pane has a header strip and its own scroll bar, and the pane last
 * clicked has the keyboard (its header is drawn dark). The editor is a
 * window of its own with the usual GEM slider. Sizes go to EMAIL.INF.
 */
#include <string.h>
#include "ui.h"
#include "../src/util.h"
#include "../src/bidi.h"

WIN w_folders, w_list, w_reader, w_editor;
static WIN *panes[] = { &w_folders, &w_list, &w_reader };
#define NPANE 3

short main_h = -1;
static GRECT mwork;			/* the main window's work area */
static WIN *focus = &w_list;
static char main_info[120];
static short sbw;			/* scroll bar width */
#define DIV 4				/* divider thickness */

#define EDKIND (NAME | CLOSER | FULLER | MOVER | INFO | SIZER | UPARROW | DNARROW | VSLIDE)
/* no SIZER: with no sliders of its own the AES would keep an empty
   column down the right edge for it. The main window draws its own size
   box below the message pane's scroll bar instead. */
#define MAINKIND (NAME | CLOSER | FULLER | MOVER | INFO)

static int intersect(GRECT *a, GRECT *b)
{
	short x1 = a->x > b->x ? a->x : b->x;
	short y1 = a->y > b->y ? a->y : b->y;
	short x2 = (a->x + a->w < b->x + b->w) ? a->x + a->w : b->x + b->w;
	short y2 = (a->y + a->h < b->y + b->h) ? a->y + a->h : b->y + b->h;
	b->x = x1;
	b->y = y1;
	b->w = x2 - x1;
	b->h = y2 - y1;
	return b->w > 0 && b->h > 0;
}

static int inside(GRECT *r, short x, short y)
{
	return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

WIN *win_find(short handle)
{
	if (handle > 0 && handle == w_editor.h)
		return &w_editor;
	return 0;
}

long win_rows(WIN *w)
{
	long r = (w->work.h - w->head_h) / (w->line_h ? w->line_h : ch);
	return r > 0 ? r : 1;
}

static short header_h(void)
{
	return ch + 4;
}

/* ---------------- layout ---------------- */

static void set_pane(WIN *w, short x, short y, short wd, short ht)
{
	w->box.x = x;
	w->box.y = y;
	w->box.w = wd;
	w->box.h = ht;
	w->work.x = x;
	w->work.y = y + header_h();
	w->work.w = wd - sbw;
	w->work.h = ht - header_h();
	if (w->work.h < 0)
		w->work.h = 0;
	w->line_h = ch;
}

/* place the panes in the main window from opt.pane_w / pane_h */
static void layout(void)
{
	short fw = opt.pane_w, lh = opt.pane_h, i;
	short minw = 10 * cw + sbw, minh = header_h() + 3 * ch;
	sbw = cw * 2 > 16 ? 16 : cw * 2;
	if (!fw)
		fw = 24 * cw + sbw;
	if (!lh)
		lh = mwork.h * 2 / 5;
	if (fw > mwork.w - minw - DIV)
		fw = mwork.w - minw - DIV;
	if (fw < minw)
		fw = minw;
	if (lh > mwork.h - minh - DIV)
		lh = mwork.h - minh - DIV;
	if (lh < minh)
		lh = minh;
	opt.pane_w = fw;
	opt.pane_h = lh;
	set_pane(&w_folders, mwork.x, mwork.y, fw, mwork.h);
	set_pane(&w_list, mwork.x + fw + DIV, mwork.y, mwork.w - fw - DIV, lh);
	set_pane(&w_reader, mwork.x + fw + DIV, mwork.y + lh + DIV, mwork.w - fw - DIV, mwork.h - lh - DIV);
	w_list.head_h = ch + 3;
	for (i = 0; i < NPANE; i++) {
		WIN *w = panes[i];
		long max = w->total - win_rows(w);
		if (w->top > max)
			w->top = max > 0 ? max : 0;
		if (w->resized)
			w->resized(w);
	}
}

static GRECT div_v(void)
{
	GRECT r;
	r.x = w_folders.box.x + w_folders.box.w;
	r.y = mwork.y;
	r.w = DIV;
	r.h = mwork.h;
	return r;
}

static GRECT div_h(void)
{
	GRECT r;
	r.x = w_list.box.x;
	r.y = w_list.box.y + w_list.box.h;
	r.w = w_list.box.w;
	r.h = DIV;
	return r;
}

/* ---------------- drawing ---------------- */

static GRECT sb_rect(WIN *w)
{
	GRECT r;
	r.x = w->box.x + w->box.w - sbw;
	r.y = w->work.y;
	r.w = sbw;
	r.h = w->work.h;
	if (w == &w_reader)
		r.h -= sbw;		/* the size box goes below it */
	return r;
}

/* the main window's size box: the bottom right corner */
static GRECT grip_rect(void)
{
	GRECT r;
	r.w = sbw;
	r.h = sbw;
	r.x = mwork.x + mwork.w - sbw;
	r.y = mwork.y + mwork.h - sbw;
	return r;
}

/* thumb position and size inside the track (between the arrows) */
static void thumb(WIN *w, short *ty, short *th, short *track_y, short *track_h)
{
	GRECT s = sb_rect(w);
	long rows = win_rows(w);
	short ah = sbw;
	*track_y = s.y + ah;
	*track_h = s.h - 2 * ah;
	if (*track_h < 4)
		*track_h = 4;
	if (w->total <= rows) {
		*ty = *track_y;
		*th = *track_h;
		return;
	}
	*th = (short)(*track_h * rows / w->total);
	if (*th < 8)
		*th = 8;
	*ty = *track_y + (short)((long)(*track_h - *th) * w->top / (w->total - rows));
}

static void box_outline(short x, short y, short wd, short ht)
{
	short p[10];
	p[0] = x;
	p[1] = y;
	p[2] = x + wd - 1;
	p[3] = y;
	p[4] = x + wd - 1;
	p[5] = y + ht - 1;
	p[6] = x;
	p[7] = y + ht - 1;
	p[8] = x;
	p[9] = y;
	vswr_mode(vdi_h, 1);
	vsl_color(vdi_h, 1);
	v_pline(vdi_h, 5, p);
}

static void arrow(short x, short y, short wd, short ht, int up)
{
	short cx = x + wd / 2, p[6], k = (ht - 4) / 2 > 3 ? 3 : (ht - 4) / 2;
	short cy = y + ht / 2;
	box_outline(x, y, wd, ht);
	if (k < 1)
		k = 1;
	p[0] = cx - k;
	p[1] = up ? cy + k / 2 + 1 : cy - k / 2 - 1;
	p[2] = cx;
	p[3] = up ? cy - k / 2 - 1 : cy + k / 2 + 1;
	p[4] = cx + k;
	p[5] = p[1];
	v_pline(vdi_h, 3, p);
}

static void draw_scrollbar(WIN *w)
{
	GRECT s = sb_rect(w), t;
	short ty, th, try_, trh;
	if (s.h <= 0)
		return;
	fill(&s, 0);
	arrow(s.x, s.y, s.w, sbw, 1);
	arrow(s.x, s.y + s.h - sbw, s.w, sbw, 0);
	thumb(w, &ty, &th, &try_, &trh);
	/* the track in the desktop pattern, the thumb white */
	t.x = s.x + 1;
	t.y = try_;
	t.w = s.w - 2;
	t.h = trh;
	vsf_interior(vdi_h, 2);
	vsf_style(vdi_h, 1);
	vsf_color(vdi_h, 1);
	{
		short p[4];
		p[0] = t.x;
		p[1] = t.y;
		p[2] = t.x + t.w - 1;
		p[3] = t.y + t.h - 1;
		vswr_mode(vdi_h, 1);
		vr_recfl(vdi_h, p);
	}
	t.y = ty;
	t.h = th;
	fill(&t, 0);
	box_outline(s.x, ty, s.w, th);
	box_outline(s.x, s.y, s.w, s.h);
}

/* drawn like the AES's own: a small window over a bigger one */
static void draw_grip(void)
{
	GRECT g = grip_rect();
	short a = g.w / 2, b = g.h / 2;
	fill(&g, 0);
	box_outline(g.x, g.y, g.w, g.h);
	box_outline(g.x + 3, g.y + 3, g.w - 6, g.h - 6);
	box_outline(g.x + 3, g.y + 3, a, b);
}

static void draw_header(WIN *w)
{
	GRECT h;
	short dark = (w == focus), cols = w->box.w / cw - 1;
	short il = (short)strlen(w->info), tl = (short)strlen(w->title);
	h.x = w->box.x;
	h.y = w->box.y;
	h.w = w->box.w;
	h.h = header_h();
	fill(&h, dark ? 1 : 0);
	if (il > cols - tl - 1)
		il = cols - tl - 1;
	text_at(h.x + cw / 2, h.y + 2, w->title, tl, cols, TX_BOLD | TX_LTR | (dark ? TX_INVERSE : 0));
	if (il > 0)
		text_at(h.x + w->box.w - (il + 1) * cw, h.y + 2, w->info, il, il,
			TX_LTR | (dark ? TX_INVERSE : TX_LIGHT));
	hline(h.x, h.x + h.w - 1, h.y + h.h - 1);
}

static void draw_divider(GRECT *r)
{
	short p[4];
	fill(r, 0);
	vsf_interior(vdi_h, 2);
	vsf_style(vdi_h, 4);
	vsf_color(vdi_h, 1);
	p[0] = r->x;
	p[1] = r->y;
	p[2] = r->x + r->w - 1;
	p[3] = r->y + r->h - 1;
	vswr_mode(vdi_h, 1);
	vr_recfl(vdi_h, p);
}

/* everything of the main window inside clip (already set as VDI clip) */
static void draw_main(GRECT *clip)
{
	short i;
	GRECT r;
	for (i = 0; i < NPANE; i++) {
		WIN *w = panes[i];
		r = w->box;
		if (!intersect(clip, &r))
			continue;
		r.x = w->box.x;
		r.y = w->box.y;
		r.w = w->box.w;
		r.h = header_h();
		if (intersect(clip, &r))
			draw_header(w);
		r = w->work;
		if (intersect(clip, &r) && w->draw) {
			clip_on(&r);
			w->draw(w, &r);
			clip_on(clip);
		}
		r = sb_rect(w);
		if (intersect(clip, &r))
			draw_scrollbar(w);
	}
	r = grip_rect();
	if (intersect(clip, &r))
		draw_grip();
	r = div_v();
	if (intersect(clip, &r))
		draw_divider(&r);
	r = div_h();
	if (intersect(clip, &r))
		draw_divider(&r);
}

/* redraw area of the main window through its rectangle list */
static void main_redraw(GRECT *area)
{
	GRECT r, a;
	if (main_h <= 0)
		return;
	a = area ? *area : mwork;
	if (!intersect(&mwork, &a))
		return;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	wind_get(main_h, WF_FIRSTXYWH, &r.x, &r.y, &r.w, &r.h);
	while (r.w > 0 && r.h > 0) {
		GRECT c = r;
		if (intersect(&a, &c)) {
			clip_on(&c);
			draw_main(&c);
		}
		wind_get(main_h, WF_NEXTXYWH, &r.x, &r.y, &r.w, &r.h);
	}
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
}

void win_redraw(WIN *w, GRECT *area)
{
	GRECT r, a;
	if (w->pane) {
		a = area ? *area : w->work;
		if (intersect(&w->box, &a))
			main_redraw(&a);
		return;
	}
	if (w->h <= 0 || !w->draw)
		return;
	a = area ? *area : w->work;
	if (!intersect(&w->work, &a))
		return;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	wind_get(w->h, WF_FIRSTXYWH, &r.x, &r.y, &r.w, &r.h);
	while (r.w > 0 && r.h > 0) {
		GRECT c = r;
		if (intersect(&a, &c)) {
			clip_on(&c);
			w->draw(w, &c);
		}
		wind_get(w->h, WF_NEXTXYWH, &r.x, &r.y, &r.w, &r.h);
	}
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
}

void win_redraw_all(void)
{
	main_redraw(0);
}

void win_redraw_lines(WIN *w, long first, long n)
{
	GRECT a;
	long y = (first - w->top) * w->line_h;
	if (y < 0) {
		n += y / w->line_h;
		y = 0;
	}
	if (n <= 0)
		return;
	a.x = w->work.x;
	a.y = w->work.y + w->head_h + (short)y;
	a.w = w->work.w;
	a.h = (short)(n * w->line_h);
	win_redraw(w, &a);
}

void win_sliders(WIN *w)
{
	long rows = win_rows(w), size, pos;
	if (w->pane) {
		GRECT s = sb_rect(w);
		main_redraw(&s);
		return;
	}
	if (w->h <= 0)
		return;
	if (w->total <= rows) {
		size = 1000;
		pos = 0;
	} else {
		size = rows * 1000L / w->total;
		pos = w->top * 1000L / (w->total - rows);
	}
	if (size < 30)
		size = 30;
	if (pos > 1000)
		pos = 1000;
	wind_set(w->h, WF_VSLSIZE, (short)size, 0, 0, 0);
	wind_set(w->h, WF_VSLIDE, (short)pos, 0, 0, 0);
}

/* GEM draws titles as stored: lay Hebrew out right to left first */
static void visual(char *dst, const char *src, int size)
{
	short n = (short)strlen(src);
	if (n >= size)
		n = size - 1;
	if (n > BIDI_MAX)
		n = BIDI_MAX;
	if (bidi_has_rtl(src, n)) {
		bidi_visual(src, n, 0, dst);
		dst[n] = 0;
	} else {
		str_copy(dst, src, size);
	}
	heb_font_map(dst, (long)strlen(dst), opt.hebfont);
}

static void redraw_header(WIN *w)
{
	GRECT h;
	h.x = w->box.x;
	h.y = w->box.y;
	h.w = w->box.w;
	h.h = header_h();
	main_redraw(&h);
}

void win_title(WIN *w, const char *title)
{
	if (w->pane) {
		/* pane headers are drawn by text_at, which lays Hebrew out */
		if (strcmp(w->title, title)) {
			str_copy(w->title, title, sizeof(w->title));
			redraw_header(w);
		}
		return;
	}
	visual(w->title, title, sizeof(w->title));
	if (w->h > 0)
		wind_set_str(w->h, WF_NAME, w->title);
}

void win_info(WIN *w, const char *info)
{
	char v[sizeof(w->info)];
	if (w->pane) {
		if (strcmp(w->info, info)) {
			str_copy(w->info, info, sizeof(w->info));
			redraw_header(w);
		}
		return;
	}
	visual(v, info, sizeof(v));
	if (!strcmp(w->info, v))
		return;
	str_copy(w->info, v, sizeof(w->info));
	if (w->h > 0)
		wind_set_str(w->h, WF_INFO, w->info);
}

/* the main window's info line: progress and status */
void main_status(const char *s)
{
	char v[sizeof(main_info)];
	visual(v, s, sizeof(v));
	if (!strcmp(v, main_info))
		return;
	str_copy(main_info, v, sizeof(main_info));
	if (main_h > 0)
		wind_set_str(main_h, WF_INFO, main_info);
}

/* ---------------- opening and closing ---------------- */

static void get_main_work(void)
{
	wind_get(main_h, WF_WORKXYWH, &mwork.x, &mwork.y, &mwork.w, &mwork.h);
}

static void save_main_place(void)
{
	wind_get(main_h, WF_CURRXYWH, &opt.main_x, &opt.main_y, &opt.main_w, &opt.main_h);
}

void main_open(void)
{
	short x = opt.main_x, y = opt.main_y, wd = opt.main_w, ht = opt.main_h;
	if (main_h > 0) {
		wind_set(main_h, WF_TOP, 0, 0, 0, 0);
		return;
	}
	/* the saved place, if it still fits this screen */
	if (wd < 30 * cw || ht < 10 * ch || x < 0 || y < desk_y ||
	    x + wd > desk_x + desk_w + 4 || y + ht > desk_y + desk_h + 4) {
		x = desk_x;
		y = desk_y;
		wd = desk_w;
		ht = desk_h;
	}
	main_h = wind_create(MAINKIND, desk_x, desk_y, desk_w, desk_h);
	if (main_h < 0) {
		alert(1, "[3][No more windows available.][ OK ]");
		return;
	}
	wind_set_str(main_h, WF_NAME, " EMail ");
	wind_set_str(main_h, WF_INFO, main_info);
	wind_open(main_h, x, y, wd, ht);
	get_main_work();
	save_main_place();
	layout();
}

void main_close(void)
{
	if (main_h <= 0)
		return;
	save_main_place();
	wind_close(main_h);
	wind_delete(main_h);
	main_h = -1;
}

/* the fonts changed: lay the panes out again */
void win_relayout(void)
{
	if (main_h > 0) {
		layout();
		main_redraw(0);
	}
	if (w_editor.h > 0) {
		if (w_editor.resized)
			w_editor.resized(&w_editor);
		w_editor.line_h = ch;
		win_sliders(&w_editor);
		win_redraw(&w_editor, 0);
	}
}

void win_open(WIN *w)
{
	if (w->pane) {
		main_open();
		return;
	}
	if (w->h > 0) {
		win_top(w);
		return;
	}
	w->line_h = ch;
	w->h = wind_create(EDKIND, desk_x, desk_y, desk_w, desk_h);
	if (w->h < 0) {
		alert(1, "[3][No more windows available.|Close a window and try again.][ OK ]");
		return;
	}
	wind_set_str(w->h, WF_NAME, w->title);
	wind_set_str(w->h, WF_INFO, w->info);
	wind_open(w->h, w->place.x, w->place.y, w->place.w, w->place.h);
	wind_get(w->h, WF_WORKXYWH, &w->work.x, &w->work.y, &w->work.w, &w->work.h);
	win_sliders(w);
}

void win_close(WIN *w)
{
	if (w->pane || w->h <= 0)
		return;
	wind_get(w->h, WF_CURRXYWH, &w->place.x, &w->place.y, &w->place.w, &w->place.h);
	if (w == &w_editor) {
		opt.ed_x = w->place.x;
		opt.ed_y = w->place.y;
		opt.ed_w = w->place.w;
		opt.ed_h = w->place.h;
	}
	wind_close(w->h);
	wind_delete(w->h);
	w->h = -1;
}

/* keyboard focus to a pane (its header goes dark) */
void win_focus(WIN *w)
{
	WIN *old = focus;
	if (!w->pane || w == focus)
		return;
	focus = w;
	redraw_header(old);
	redraw_header(w);
}

void win_top(WIN *w)
{
	if (w->pane) {
		if (main_h > 0)
			wind_set(main_h, WF_TOP, 0, 0, 0, 0);
		win_focus(w);
		return;
	}
	if (w->h > 0)
		wind_set(w->h, WF_TOP, 0, 0, 0, 0);
}

WIN *win_topmost(void)
{
	short t, d;
	wind_get(0, WF_TOP, &t, &d, &d, &d);
	if (t == main_h && main_h > 0)
		return focus;
	return win_find(t);
}

int win_is_top(WIN *w)
{
	return win_topmost() == w;
}

/* ---------------- scrolling ---------------- */

void win_scroll_to(WIN *w, long top)
{
	long rows = win_rows(w), max = w->total - rows;
	if (max < 0)
		max = 0;
	if (top > max)
		top = max;
	if (top < 0)
		top = 0;
	if (top == w->top)
		return;
	w->top = top;
	win_sliders(w);
	{
		GRECT a = w->work;
		a.y += w->head_h;
		a.h -= w->head_h;
		win_redraw(w, &a);
	}
	if (w->scrolled)
		w->scrolled(w);
}

void win_ensure_visible(WIN *w, long line)
{
	long rows = win_rows(w);
	if (line < w->top)
		win_scroll_to(w, line);
	else if (line >= w->top + rows)
		win_scroll_to(w, line - rows + 1);
}

/* the pointer shape win_hover() last set; drags set it back to ARROW */
static short hover_shape = ARROW;

static short button_down(short *mx, short *my)
{
	short mb, ks;
	graf_mkstate(mx, my, &mb, &ks);
	return mb & 3;
}

/* a press on a pane's scroll bar */
static void scrollbar_press(WIN *w, short my)
{
	GRECT s = sb_rect(w);
	short ty, th, try_, trh, mx, y;
	long rows = win_rows(w);
	thumb(w, &ty, &th, &try_, &trh);
	if (my < s.y + sbw || my >= s.y + s.h - sbw) {
		/* an arrow: one line, repeating while held */
		short d = my < s.y + sbw ? -1 : 1, first = 1;
		do {
			win_scroll_to(w, w->top + d);
			evnt_timer_(first ? 250 : 40);
			first = 0;
		} while (button_down(&mx, &y));
		return;
	}
	if (my < ty || my >= ty + th) {
		/* the track: a page */
		win_scroll_to(w, w->top + (my < ty ? -(rows - 1) : rows - 1));
		return;
	}
	/* the thumb: follow the mouse */
	{
		short grab = my - ty;
		wind_update(3);
		while (button_down(&mx, &y)) {
			long max = w->total - rows;
			if (max > 0 && trh > th)
				win_scroll_to(w, (long)(y - grab - try_) * max / (trh - th));
			evnt_timer_(20);
		}
		wind_update(2);
	}
}

/* drag a divider; vertical: between folders and the rest */
static void divider_drag(int vertical)
{
	short mx, my, last = -1, p[4], held;
	GRECT lim = mwork;
	wind_update(BEG_UPDATE);
	wind_update(3);
	graf_mouse(FLAT_HAND, 0);		/* as Claude ST's divider */
	clip_on(&lim);
	vswr_mode(vdi_h, 3);			/* XOR */
	vsl_color(vdi_h, 1);
	/* do..while: the press may have been reported only after the
	   release, then the pointer's place is where it was dropped */
	do {
		short pos;
		held = button_down(&mx, &my);
		pos = vertical ? mx : my;
		if (pos != last) {
			graf_mouse(M_OFF, 0);
			if (last >= 0)
				v_pline(vdi_h, 2, p);
			if (vertical) {
				p[0] = p[2] = mx;
				p[1] = mwork.y;
				p[3] = mwork.y + mwork.h - 1;
			} else {
				p[0] = w_list.box.x;
				p[2] = w_list.box.x + w_list.box.w - 1;
				p[1] = p[3] = my;
			}
			v_pline(vdi_h, 2, p);
			graf_mouse(M_ON, 0);
			last = pos;
		}
		evnt_timer_(10);
	} while (held);
	if (last >= 0) {
		graf_mouse(M_OFF, 0);
		v_pline(vdi_h, 2, p);
		graf_mouse(M_ON, 0);
	}
	vswr_mode(vdi_h, 1);
	graf_mouse(ARROW, 0);
	hover_shape = ARROW;
	wind_update(2);
	wind_update(END_UPDATE);
	if (last < 0)
		return;
	if (vertical)
		opt.pane_w = last - mwork.x;
	else
		opt.pane_h = last - mwork.y;
	layout();
	main_redraw(0);
}

/* where a divider can be grabbed: a little wider than it is drawn */
static int on_divider(short mx, short my, int vertical)
{
	GRECT r = vertical ? div_v() : div_h();
	if (main_h <= 0)
		return 0;
	if (vertical) {
		r.x -= 3;
		r.w += 6;
	} else {
		r.y -= 3;
		r.h += 6;
	}
	return inside(&r, mx, my);
}

/* the pointer shows the sizing arrows over a divider */
void win_hover(short mx, short my)
{
	short h = wind_find(mx, my), t, d, want = ARROW;
	wind_get(0, WF_TOP, &t, &d, &d, &d);
	if (h == main_h && t == main_h) {
		GRECT g = grip_rect();
		/* the flat hand, as over Claude ST's divider */
		if (inside(&g, mx, my) || on_divider(mx, my, 1) || on_divider(mx, my, 0))
			want = FLAT_HAND;
	}
	if (want != hover_shape) {
		graf_mouse(want, 0);
		hover_shape = want;
	}
	/* the next move wakes us again */
	evnt_set_m1(1, mx, my, 1, 1);
}

/* drag the size box. EMail draws the outline itself, the way the
   dividers are dragged: graf_rubberbox() hung TOS 4's AES here. */
static void xor_box(GRECT *r)
{
	short p[10];
	p[0] = p[6] = p[8] = r->x;
	p[1] = p[3] = p[9] = r->y;
	p[2] = p[4] = r->x + r->w - 1;
	p[5] = p[7] = r->y + r->h - 1;
	graf_mouse(M_OFF, 0);
	v_pline(vdi_h, 5, p);
	graf_mouse(M_ON, 0);
}

static void main_resize(void)
{
	GRECT c, b, scr;
	short mx, my, ox, oy, held, shown = 0, minw = 40 * cw, minh = 12 * ch;
	wind_get(main_h, WF_CURRXYWH, &c.x, &c.y, &c.w, &c.h);
	button_down(&mx, &my);
	ox = c.x + c.w - mx;		/* where in the box it was grabbed */
	oy = c.y + c.h - my;
	b = c;
	scr.x = 0;
	scr.y = 0;
	scr.w = scr_w;
	scr.h = scr_h;
	wind_update(BEG_UPDATE);
	wind_update(3);
	graf_mouse(FLAT_HAND, 0);
	clip_on(&scr);
	vswr_mode(vdi_h, 3);		/* XOR */
	vsl_color(vdi_h, 1);
	do {
		GRECT n = c;
		held = button_down(&mx, &my);
		n.w = mx + ox - c.x;
		n.h = my + oy - c.y;
		if (n.w < minw)
			n.w = minw;
		if (n.h < minh)
			n.h = minh;
		if (c.x + n.w > scr_w)
			n.w = scr_w - c.x;
		if (c.y + n.h > scr_h)
			n.h = scr_h - c.y;
		if (!shown || n.w != b.w || n.h != b.h) {
			if (shown)
				xor_box(&b);
			b = n;
			xor_box(&b);
			shown = 1;
		}
		evnt_timer_(10);
	} while (held);
	xor_box(&b);
	vswr_mode(vdi_h, 1);
	graf_mouse(ARROW, 0);
	hover_shape = ARROW;
	wind_update(2);
	wind_update(END_UPDATE);
	if (b.w == c.w && b.h == c.h)
		return;
	wind_set(main_h, WF_CURRXYWH, c.x, c.y, b.w, b.h);
	get_main_work();
	save_main_place();
	layout();
	main_redraw(0);
}

/* a mouse press anywhere; returns 1 if it was ours */
int win_mouse(short mx, short my, short button, short clicks, short kstate)
{
	short h = wind_find(mx, my), i;
	GRECT r;
	if (h > 0 && h == main_h) {
		r = grip_rect();
		if (inside(&r, mx, my)) {
			main_resize();
			return 1;
		}
		if (on_divider(mx, my, 1)) {
			divider_drag(1);
			return 1;
		}
		if (on_divider(mx, my, 0)) {
			divider_drag(0);
			return 1;
		}
		for (i = 0; i < NPANE; i++) {
			WIN *w = panes[i];
			if (!inside(&w->box, mx, my))
				continue;
			win_focus(w);
			r = sb_rect(w);
			if (inside(&r, mx, my)) {
				scrollbar_press(w, my);
			} else if (inside(&w->work, mx, my)) {
				if ((button & 2) && w->rclick)
					w->rclick(w, mx, my);
				else if (w->click)
					w->click(w, mx, my, clicks, kstate);
			}
			return 1;
		}
		return 1;
	}
	if (h > 0 && h == w_editor.h && inside(&w_editor.work, mx, my)) {
		if ((button & 2) && w_editor.rclick)
			w_editor.rclick(&w_editor, mx, my);
		else if (w_editor.click)
			w_editor.click(&w_editor, mx, my, clicks, kstate);
		return 1;
	}
	return 0;
}

/* ---------------- AES messages ---------------- */

void main_closed(void);

static void main_message(short *msg)
{
	GRECT r;
	switch (msg[0]) {
	case WM_REDRAW:
		r.x = msg[4];
		r.y = msg[5];
		r.w = msg[6];
		r.h = msg[7];
		main_redraw(&r);
		break;
	case WM_TOPPED:
	case WM_NEWTOP: {
		short mx, my, mb, ks;
		wind_set(main_h, WF_TOP, 0, 0, 0, 0);
		/* the AES tops a window on a click in its contents and keeps
		   the click: act on it as well, as a modern program does */
		graf_mkstate(&mx, &my, &mb, &ks);
		if (msg[0] == WM_TOPPED && inside(&mwork, mx, my))
			win_mouse(mx, my, mb ? mb : 1, 1, ks);
		break;
	}
	case WM_CLOSED:
		main_closed();
		break;
	case WM_FULLED: {
		GRECT c, f, p;
		wind_get(main_h, WF_CURRXYWH, &c.x, &c.y, &c.w, &c.h);
		wind_get(main_h, WF_FULLXYWH, &f.x, &f.y, &f.w, &f.h);
		if (c.x == f.x && c.y == f.y && c.w == f.w && c.h == f.h) {
			wind_get(main_h, WF_PREVXYWH, &p.x, &p.y, &p.w, &p.h);
			wind_set(main_h, WF_CURRXYWH, p.x, p.y, p.w, p.h);
		} else {
			wind_set(main_h, WF_CURRXYWH, f.x, f.y, f.w, f.h);
		}
		get_main_work();
		save_main_place();
		layout();
		main_redraw(0);
		break;
	}
	case WM_SIZED:
	case WM_MOVED:
		if (msg[6] < 40 * cw)
			msg[6] = 40 * cw;
		if (msg[7] < 12 * ch)
			msg[7] = 12 * ch;
		wind_set(main_h, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
		get_main_work();
		save_main_place();
		layout();
		if (msg[0] == WM_SIZED)
			main_redraw(0);
		break;
	}
}

void win_message(short *msg)
{
	WIN *w;
	GRECT r;
	if (msg[3] == main_h && main_h > 0) {
		main_message(msg);
		menu_update();
		return;
	}
	w = win_find(msg[3]);
	if (!w)
		return;
	switch (msg[0]) {
	case WM_REDRAW:
		r.x = msg[4];
		r.y = msg[5];
		r.w = msg[6];
		r.h = msg[7];
		win_redraw(w, &r);
		break;
	case WM_TOPPED:
	case WM_NEWTOP: {
		short mx, my, mb, ks;
		win_top(w);
		graf_mkstate(&mx, &my, &mb, &ks);
		if (msg[0] == WM_TOPPED && w->click && inside(&w->work, mx, my))
			w->click(w, mx, my, 1, ks);
		menu_update();
		break;
	}
	case WM_CLOSED:
		if (w->closed)
			w->closed(w);
		else
			win_close(w);
		menu_update();
		break;
	case WM_FULLED: {
		GRECT c, f, p;
		wind_get(w->h, WF_CURRXYWH, &c.x, &c.y, &c.w, &c.h);
		wind_get(w->h, WF_FULLXYWH, &f.x, &f.y, &f.w, &f.h);
		if (c.x == f.x && c.y == f.y && c.w == f.w && c.h == f.h) {
			wind_get(w->h, WF_PREVXYWH, &p.x, &p.y, &p.w, &p.h);
			wind_set(w->h, WF_CURRXYWH, p.x, p.y, p.w, p.h);
		} else {
			wind_set(w->h, WF_CURRXYWH, f.x, f.y, f.w, f.h);
		}
		wind_get(w->h, WF_WORKXYWH, &w->work.x, &w->work.y, &w->work.w, &w->work.h);
		if (w->resized)
			w->resized(w);
		win_scroll_to(w, w->top);
		win_sliders(w);
		win_redraw(w, 0);
		break;
	}
	case WM_SIZED:
	case WM_MOVED:
		if (msg[6] < 12 * cw)
			msg[6] = 12 * cw;
		if (msg[7] < 6 * ch)
			msg[7] = 6 * ch;
		wind_set(w->h, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
		wind_get(w->h, WF_WORKXYWH, &w->work.x, &w->work.y, &w->work.w, &w->work.h);
		if (w == &w_editor) {
			opt.ed_x = msg[4];
			opt.ed_y = msg[5];
			opt.ed_w = msg[6];
			opt.ed_h = msg[7];
		}
		if (msg[0] == WM_SIZED) {
			if (w->resized)
				w->resized(w);
			win_scroll_to(w, w->top);
			win_sliders(w);
			win_redraw(w, 0);
		}
		break;
	case WM_ARROWED: {
		long rows = win_rows(w);
		switch (msg[4]) {
		case 0: win_scroll_to(w, w->top - rows); break;
		case 1: win_scroll_to(w, w->top + rows); break;
		case 2: win_scroll_to(w, w->top - 1); break;
		case 3: win_scroll_to(w, w->top + 1); break;
		}
		break;
	}
	case WM_VSLID: {
		long rows = win_rows(w), max = w->total - rows;
		if (max > 0)
			win_scroll_to(w, (long)msg[4] * max / 1000);
		break;
	}
	}
}
