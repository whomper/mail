/*
 * list.c - the message list window (the GFA Troll's "headers" window):
 * newest first, unread in bold, flag/attachment marks, and the From (or
 * To, in Sent and Outbox) and Subject columns laid out right to left
 * when they are Hebrew.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "../src/mail.h"
#include "../src/util.h"

static long *view;		/* indexes into cur_folder->h, newest first */
static long nview, sel = -1;
static long shown;		/* local folders: how many of the newest to list */
static long more;		/* older messages not listed yet (0: no "load more" row) */
static unsigned long sel_uid;

static int cmp_date(const void *x, const void *y)
{
	HDR *a = &cur_folder->h[*(const long *)x], *b = &cur_folder->h[*(const long *)y];
	if (a->date != b->date)
		return a->date < b->date ? 1 : -1;
	return a->uid < b->uid ? 1 : a->uid > b->uid ? -1 : 0;
}

static int show_to(void)
{
	return cur_finfo && (cur_finfo->role == FR_SENT || cur_finfo->role == FR_OUTBOX ||
			     cur_finfo->role == FR_DRAFTS);
}

void list_titles(void)
{
	char t[100], i[120];
	if (!cur_folder) {
		win_title(&w_list, "Messages");
		win_info(&w_list, "");
		return;
	}
	snprintf(t, sizeof(t), "%s - %s", cur_finfo->disp, cur_acct->name);
	win_title(&w_list, t);
	if (opt.offline)
		snprintf(i, sizeof(i), "%s unread  (offline)", num(cur_finfo->unread));
	else if (cur_acct->cut && !cur_finfo->local)
		snprintf(i, sizeof(i), "%s unread  (not connected: Check mail tries again)", num(cur_finfo->unread));
	else
		snprintf(i, sizeof(i), "%s of %s, %s unread", num(cur_folder->n),
			 num(cur_finfo->total > cur_folder->n ? cur_finfo->total : cur_folder->n),
			 num(cur_finfo->unread));
	win_info(&w_list, i);
}

void list_refresh(void)
{
	long i;
	free(view);
	view = 0;
	nview = 0;
	sel = -1;
	if (cur_folder && cur_folder->n) {
		view = malloc(cur_folder->n * sizeof(long));
		if (view) {
			for (i = 0; i < cur_folder->n; i++)
				view[i] = i;
			nview = cur_folder->n;
			qsort(view, nview, sizeof(long), cmp_date);
			/* a big local folder lists a page at a time too */
			if (cur_finfo->local && nview > shown)
				nview = shown;
			for (i = 0; i < nview; i++)
				if (cur_folder->h[view[i]].uid == sel_uid)
					sel = i;
		}
	}
	if (cur_folder)
		fold_count(cur_folder);
	more = 0;
	if (cur_folder && cur_finfo->local)
		more = cur_folder->n - nview;
	else if (cur_folder) {
		long server = cur_folder->exists ? cur_folder->exists : cur_finfo->total;
		more = server - cur_folder->n;
	}
	if (more < 0)
		more = 0;
	w_list.total = nview + (more ? 1 : 0);
	if (w_list.top > nview)
		w_list.top = 0;
	list_titles();
	win_sliders(&w_list);
	win_redraw(&w_list, 0);
}

void list_load(void)
{
	sel_uid = 0;
	shown = opt.page;
	w_list.top = 0;
	list_refresh();
}

HDR *list_current(void)
{
	if (!cur_folder || sel < 0 || sel >= nview)
		return 0;
	return &cur_folder->h[view[sel]];
}

void list_select_uid(unsigned long uid)
{
	long i;
	sel_uid = uid;
	for (i = 0; i < nview; i++)
		if (cur_folder->h[view[i]].uid == uid) {
			sel = i;
			win_ensure_visible(&w_list, i);
		}
	win_redraw(&w_list, 0);
}

/* after the current message was deleted or moved: show the next one */
void list_after_remove(void)
{
	long keep = sel;
	list_refresh();
	if (nview) {
		if (keep >= nview)
			keep = nview - 1;
		if (keep < 0)
			keep = 0;
		sel = keep;
		sel_uid = cur_folder->h[view[sel]].uid;
		win_redraw(&w_list, 0);
		reader_show(&cur_folder->h[view[sel]]);
	} else {
		reader_clear();
	}
	folders_build();
}

static short wide;	/* room for the year and the size column */

/* the marks column: ! (flagged) and @ (attachments), then a space.
   Unread messages are bold. */
#define MK 3

static void columns(short cols, short *c_from, short *c_subj, short *c_date)
{
	short rest;
	wide = cols >= 76;
	*c_date = wide ? 15 : 12;
	rest = cols - MK - *c_date - (wide ? 6 : 0);
	*c_from = rest / 3;
	if (*c_from > 24)
		*c_from = 24;
	if (*c_from < 8)
		*c_from = 8;
	*c_subj = rest - *c_from - 1;
	if (*c_subj < 4)
		*c_subj = 4;
}

static void size_str(long n, char *out)
{
	if (n < 1024)
		snprintf(out, 8, "%ldb", n);
	else if (n < 1024L * 1000)
		snprintf(out, 8, "%ldK", (n + 512) / 1024);
	else
		snprintf(out, 8, "%ldM", (n + 512L * 1024) / (1024L * 1024));
}

static void draw(WIN *w, GRECT *clip)
{
	short cols = w->work.w / cw, c_from, c_subj, c_date;
	long i, rows = win_rows(w) + 1;
	short x0 = w->work.x + 2;
	GRECT r;
	columns(cols, &c_from, &c_subj, &c_date);
	fill(clip, 0);

	/* column titles */
	{
		const char *who = show_to() ? "To" : "From";
		text_at(x0 + MK * cw, w->work.y + 1, who, strlen(who), c_from, TX_BOLD);
		text_at(x0 + (MK + 1 + c_from) * cw, w->work.y + 1, "Subject", 7, c_subj, TX_BOLD);
		text_at(x0 + (MK + 1 + c_from + c_subj) * cw, w->work.y + 1, "Date", 4, c_date, TX_BOLD);
		hline(w->work.x, w->work.x + w->work.w - 1, w->work.y + w->head_h - 2);
	}
	if (!cur_folder) {
		text_at(x0, w->work.y + w->head_h + ch, "Choose a folder on the left.", 28, cols, TX_LIGHT);
		return;
	}
	if (!nview) {
		const char *m = cur_finfo->role == FR_OUTBOX ? "Nothing waiting to be sent." : "No messages.";
		text_at(x0, w->work.y + w->head_h + ch, m, strlen(m), cols, TX_LIGHT);
		return;
	}
	if (more && nview >= w->top && nview < w->top + rows) {
		/* the last row offers the next page */
		char t[80];
		short y = w->work.y + w->head_h + (short)((nview - w->top) * ch);
		long next = more < opt.page ? more : opt.page;
		snprintf(t, sizeof(t), "\x02 Load %s more  (%s older on %s)", num(next), num(more),
			 cur_finfo->local ? "disk" : "the server");
		r.x = w->work.x;
		r.y = y;
		r.w = w->work.w;
		r.h = ch;
		fill(&r, sel == nview ? 1 : 0);
		text_at(x0 + MK * cw, y, t, strlen(t), cols - MK, TX_BOLD | (sel == nview ? TX_INVERSE : 0));
	}
	for (i = w->top; i < nview && i < w->top + rows; i++) {
		HDR *h = &cur_folder->h[view[i]];
		short y = w->work.y + w->head_h + (short)((i - w->top) * ch);
		short fl = (i == sel) ? TX_INVERSE : 0;
		char marks[4], date[24], size[8];
		const char *who = show_to() ? h->to : h->from;
		char name[64];
		if (i == sel) {
			r.x = w->work.x;
			r.y = y;
			r.w = w->work.w;
			r.h = ch;
			fill(&r, 1);
		}
		if (!(h->flags & MF_SEEN))
			fl |= TX_BOLD;
		/* fixed places: ! flagged, @ attachments */
		marks[0] = (h->flags & MF_FLAGGED) ? '!' : ' ';
		marks[1] = (h->flags & MF_ATTACH) ? '@' : ' ';
		marks[2] = 0;
		text_at(x0, y, marks, 2, 2, fl & ~TX_BOLD);
		/* "Name <a@b>" shows as Name */
		addr_split(who ? who : "", name, sizeof(name), 0, 0);
		if (!name[0])
			addr_split(who ? who : "", 0, 0, name, sizeof(name));
		text_at(x0 + MK * cw, y, name, strlen(name), c_from, fl | TX_RIGHT);
		text_at(x0 + (MK + 1 + c_from) * cw, y, h->subject, strlen(h->subject), c_subj - 1, fl | TX_RIGHT);
		date_str(h->date, date, sizeof(date), wide);
		text_at(x0 + (MK + 1 + c_from + c_subj) * cw, y, date, strlen(date), c_date, fl & ~TX_BOLD);
		if (wide) {
			size_str(h->size, size);
			text_at(x0 + (cols - 6) * cw, y, size, strlen(size), 5, fl & ~TX_BOLD);
		}
	}
}

/* the "load more" row: the next page of older messages */
static void load_more(void)
{
	long added = 0, keep_top = w_list.top;
	if (!more)
		return;
	if (cur_finfo->local) {
		shown += opt.page;
	} else if (opt.offline || cur_acct->cut) {
		alert(1, "[1][MAIL is not connected.|Older messages are on the server:|Check mail to connect again.][ OK ]");
		return;
	} else {
		busy(1);
		mail_err[0] = 0;
		if (!mail_load_more(cur_acct, cur_finfo, &added) && mail_err[0])
			alert(1, "[1][%s][ OK ]", mail_err);
		busy(0);
		fold_close(cur_folder);
		cur_folder = fold_open(cur_acct, cur_finfo);
	}
	list_refresh();
	win_scroll_to(&w_list, keep_top);
	folders_build();
}

static void select_row(long i, int open)
{
	long old = sel;
	if (i >= nview && more) {
		/* onto the "load more" row; Return (or a click) loads */
		sel = nview;
		win_redraw_lines(&w_list, old, 1);
		win_redraw_lines(&w_list, sel, 1);
		win_ensure_visible(&w_list, nview);
		if (open)
			load_more();
		return;
	}
	if (i < 0 || i >= nview)
		return;
	sel = i;
	sel_uid = cur_folder->h[view[i]].uid;
	win_redraw_lines(&w_list, old, 1);
	win_redraw_lines(&w_list, sel, 1);
	win_ensure_visible(&w_list, sel);
	reader_show(&cur_folder->h[view[i]]);
	/* reading may have marked it read */
	win_redraw_lines(&w_list, sel, 1);
	list_titles();
	folders_build();
	if (open)
		win_focus(&w_reader);
}

static void click(WIN *w, short mx, short my, short clicks, short kstate)
{
	long i;
	(void)mx;
	(void)kstate;
	if (my < w->work.y + w->head_h)
		return;
	i = w->top + (my - w->work.y - w->head_h) / ch;
	if (i == nview && more) {
		load_more();
		return;
	}
	select_row(i, clicks > 1);
}

/* the message menu, shared with the reader */
void message_menu(short mx, short my, int with_open)
{
	HDR *h = cur_folder ? fold_get(cur_folder, cur_uid) : 0;
	const char *lab[10];
	short cmd[10], n = 0, r;
	if (!h || !cur_msg)
		return;
	if (with_open) {
		lab[n] = "Open";
		cmd[n++] = -1;
	}
	lab[n] = "Reply            ^R";
	cmd[n++] = C_REPLY;
	lab[n] = "Reply to all     ^E";
	cmd[n++] = C_REPLYALL;
	lab[n] = "Forward          ^F";
	cmd[n++] = C_FORWARD;
	lab[n] = "-";
	cmd[n++] = 0;
	lab[n] = (h->flags & MF_SEEN) ? "Mark as unread   ^U" : "Mark as read";
	cmd[n++] = (h->flags & MF_SEEN) ? C_UNREAD : C_MARKREAD;
	lab[n] = (h->flags & MF_FLAGGED) ? "Remove the flag  ^G" : "Flag             ^G";
	cmd[n++] = C_FLAG;
	lab[n] = "Move to...       ^M";
	cmd[n++] = C_MOVE;
	lab[n] = "-";
	cmd[n++] = 0;
	lab[n] = "Delete          Del";
	cmd[n++] = C_DELETE;
	r = popup(mx, my, lab, n);
	if (r < 0)
		return;
	if (cmd[r] == -1)
		win_focus(&w_reader);
	else if (cmd[r])
		ui_command(cmd[r]);
}

static void rclick(WIN *w, short mx, short my)
{
	long i;
	if (my < w->work.y + w->head_h)
		return;
	i = w->top + (my - w->work.y - w->head_h) / ch;
	if (i < 0 || i >= nview)
		return;
	if (cur_folder->h[view[i]].uid != cur_uid || !cur_msg)
		select_row(i, 0);
	message_menu(mx, my, 1);
}

static int key(WIN *w, short kstate, short k)
{
	short scan = KEY_SCAN(k);
	long rows = win_rows(w);
	switch (scan) {
	case 0x48:	/* up (shift: page) */
		select_row(sel < 0 ? 0 : sel - ((kstate & 3) ? rows : 1), 0);
		return 1;
	case 0x50:	/* down */
		select_row(sel < 0 ? 0 : sel + ((kstate & 3) ? rows : 1), 0);
		return 1;
	case 0x47:	/* Clr/Home: first, shift: last */
		select_row((kstate & 3) ? nview - 1 : 0, 0);
		return 1;
	case 0x1c:	/* Return: read it (or load more, on that row) */
	case 0x72:
		select_row(sel < 0 ? 0 : sel, 1);
		return 1;
	}
	return 0;
}

void list_init(void);
void list_init(void)
{
	w_list.h = -1;
	w_list.pane = 1;
	w_list.draw = draw;
	w_list.click = click;
	w_list.rclick = rclick;
	w_list.key = key;
	strcpy(w_list.title, "Messages");
}
