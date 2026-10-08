/*
 * win.c - GEM windows with a scrolled list of lines: redraw through the
 * rectangle list, sliders, arrows, sizing and full-screen toggling.
 */
#include <string.h>
#include "ui.h"
#include "../src/util.h"
#include "../src/bidi.h"

WIN w_folders, w_list, w_reader, w_editor;
static WIN *all[] = { &w_folders, &w_list, &w_reader, &w_editor };
#define NWIN 4

#define KIND (NAME | CLOSER | FULLER | MOVER | INFO | SIZER | UPARROW | DNARROW | VSLIDE)

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

WIN *win_find(short handle)
{
	short i;
	for (i = 0; i < NWIN; i++)
		if (all[i]->h == handle && handle > 0)
			return all[i];
	return 0;
}

static void get_work(WIN *w)
{
	wind_get(w->h, WF_WORKXYWH, &w->work.x, &w->work.y, &w->work.w, &w->work.h);
}

long win_rows(WIN *w)
{
	long r = (w->work.h - w->head_h) / (w->line_h ? w->line_h : ch);
	return r > 0 ? r : 1;
}

void win_sliders(WIN *w)
{
	long rows = win_rows(w), size, pos;
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
}

void win_title(WIN *w, const char *title)
{
	visual(w->title, title, sizeof(w->title));
	if (w->h > 0)
		wind_set_str(w->h, WF_NAME, w->title);
}

void win_info(WIN *w, const char *info)
{
	char v[sizeof(w->info)];
	visual(v, info, sizeof(v));
	if (!strcmp(w->info, v))
		return;
	str_copy(w->info, v, sizeof(w->info));
	if (w->h > 0)
		wind_set_str(w->h, WF_INFO, w->info);
}

void win_open(WIN *w)
{
	if (w->h > 0) {
		win_top(w);
		return;
	}
	if (!w->line_h)
		w->line_h = ch;
	w->h = wind_create(KIND, desk_x, desk_y, desk_w, desk_h);
	if (w->h < 0) {
		alert(1, "[3][No more windows available.|Close a window and try again.][ OK ]");
		return;
	}
	wind_set_str(w->h, WF_NAME, w->title);
	wind_set_str(w->h, WF_INFO, w->info);
	wind_open(w->h, w->place.x, w->place.y, w->place.w, w->place.h);
	get_work(w);
	win_sliders(w);
}

void win_close(WIN *w)
{
	if (w->h <= 0)
		return;
	wind_get(w->h, WF_CURRXYWH, &w->place.x, &w->place.y, &w->place.w, &w->place.h);
	wind_close(w->h);
	wind_delete(w->h);
	w->h = -1;
}

void win_top(WIN *w)
{
	if (w->h > 0)
		wind_set(w->h, WF_TOP, 0, 0, 0, 0);
}

WIN *win_topmost(void)
{
	short t, d;
	wind_get(0, WF_TOP, &t, &d, &d, &d);
	return win_find(t);
}

int win_is_top(WIN *w)
{
	return w->h > 0 && win_topmost() == w;
}

void win_redraw(WIN *w, GRECT *area)
{
	GRECT r, a;
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
}

void win_ensure_visible(WIN *w, long line)
{
	long rows = win_rows(w);
	if (line < w->top)
		win_scroll_to(w, line);
	else if (line >= w->top + rows)
		win_scroll_to(w, line - rows + 1);
}

void win_message(short *msg)
{
	WIN *w = win_find(msg[3]);
	GRECT r;
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
		/* the AES tops a window on a click in its contents and keeps
		   the click: act on it as well, as a modern program does */
		graf_mkstate(&mx, &my, &mb, &ks);
		if (msg[0] == WM_TOPPED && w->click && mx >= w->work.x && my >= w->work.y &&
		    mx < w->work.x + w->work.w && my < w->work.y + w->work.h)
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
		get_work(w);
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
		get_work(w);
		if (msg[0] == WM_SIZED) {
			if (w->resized)
				w->resized(w);
			win_scroll_to(w, w->top);
			win_sliders(w);
			win_redraw(w, 0);	/* the layout depends on the width */
		}
		break;
	case WM_ARROWED: {
		long rows = win_rows(w);
		switch (msg[4]) {
		case 0: win_scroll_to(w, w->top - rows); break;		/* page up */
		case 1: win_scroll_to(w, w->top + rows); break;		/* page down */
		case 2: win_scroll_to(w, w->top - 1); break;		/* line up */
		case 3: win_scroll_to(w, w->top + 1); break;		/* line down */
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
