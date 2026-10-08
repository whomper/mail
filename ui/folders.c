/*
 * folders.c - the folders window: every account with its folders and
 * unread counts (the GFA Troll's "groups" window). Clicking a folder opens it
 * in the message list; IMAP folders are synchronised first.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "../src/mail.h"
#include "../src/util.h"

ACCOUNT *cur_acct;
FINFO *cur_finfo;
FOLDER *cur_folder;

typedef struct {
	ACCOUNT *a;
	FINFO *fi;	/* NULL: the account's own line */
} ENTRY;

static ENTRY ent[MAXACCT * (MAXFOLDER + 1)];
static long nent, sel = -1;

static short role_rank(FINFO *f)
{
	switch (f->role) {
	case FR_INBOX: return 0;
	case FR_OUTBOX: return 1;
	case FR_DRAFTS: return 2;
	case FR_SENT: return 3;
	case FR_ARCHIVE: return 4;
	case FR_JUNK: return 5;
	case FR_TRASH: return 6;
	}
	return 7;
}

/* special folders first, the rest by name (children follow parents) */
static int cmp_folder(const void *x, const void *y)
{
	FINFO *a = ((const ENTRY *)x)->fi, *b = ((const ENTRY *)y)->fi;
	short ra = role_rank(a), rb = role_rank(b);
	if (ra != rb)
		return ra - rb;
	return strcmp(a->server, b->server);
}

void folders_build(void)
{
	short i, j;
	nent = 0;
	for (i = 0; i < naccts; i++) {
		ACCOUNT *a = accts[i];
		long first;
		ent[nent].a = a;
		ent[nent++].fi = 0;
		first = nent;
		for (j = 0; j < a->nfolders; j++) {
			ent[nent].a = a;
			ent[nent++].fi = &a->folders[j];
		}
		qsort(ent + first, nent - first, sizeof(ENTRY), cmp_folder);
	}
	sel = -1;
	for (i = 0; i < nent; i++)
		if (ent[i].fi && ent[i].fi == cur_finfo)
			sel = i;
	w_folders.total = nent;
	win_sliders(&w_folders);
	win_redraw(&w_folders, 0);
}

static void draw(WIN *w, GRECT *clip)
{
	long i, rows = win_rows(w) + 1;
	short cols = w->work.w / cw;
	fill(clip, 0);
	for (i = w->top; i < nent && i < w->top + rows; i++) {
		ENTRY *e = &ent[i];
		short y = w->work.y + (short)((i - w->top) * ch);
		short flags = (i == sel) ? TX_INVERSE : 0;
		char cnt[16];
		if (i == sel) {
			GRECT r;
			r.x = w->work.x;
			r.y = y;
			r.w = w->work.w;
			r.h = ch;
			fill(&r, 1);
		}
		if (!e->fi) {
			text_at(w->work.x + 2, y, e->a->name, strlen(e->a->name), cols, flags | TX_BOLD);
			continue;
		}
		{
			short ind = 1 + e->fi->depth * 2;
			short room = cols - ind;
			cnt[0] = 0;
			if (e->fi->unread > 0)
				snprintf(cnt, sizeof(cnt), " %ld", e->fi->unread);
			else if (e->fi->role == FR_OUTBOX && e->fi->total > 0)
				snprintf(cnt, sizeof(cnt), " %ld", e->fi->total);
			room -= (short)strlen(cnt) + 1;
			text_at(w->work.x + 2 + ind * cw, y, e->fi->disp, strlen(e->fi->disp), room,
				flags | (e->fi->noselect ? TX_LIGHT : 0));
			if (cnt[0])
				text_at(w->work.x + (cols - 1 - (short)strlen(cnt)) * cw, y, cnt, strlen(cnt), 8,
					flags | TX_BOLD);
		}
	}
}

void folders_select(ACCOUNT *a, FINFO *fi)
{
	long newmsgs, i;
	if (!fi || fi->noselect)
		return;
	if (cur_folder) {
		fold_close(cur_folder);
		cur_folder = 0;
	}
	cur_acct = a;
	cur_finfo = fi;
	for (i = 0; i < nent; i++)
		if (ent[i].fi == fi)
			sel = i;
	win_redraw(&w_folders, 0);
	reader_clear();
	/* show what we have at once, then bring it up to date */
	cur_folder = fold_open(a, fi);
	list_load();
	if (!fi->local && !opt.offline) {
		busy(1);
		mail_err[0] = 0;
		if (!mail_sync_folder(a, fi, &newmsgs) && mail_err[0])
			alert(1, "[1][%s][ OK ]", mail_err);
		busy(0);
		if (cur_folder)
			fold_close(cur_folder);
		cur_folder = fold_open(a, fi);
		list_load();
		win_redraw(&w_folders, 0);
	}
	menu_update();
}

static void click(WIN *w, short mx, short my, short clicks, short kstate)
{
	long i = w->top + (my - w->work.y) / ch;
	(void)mx;
	(void)clicks;
	(void)kstate;
	if (i < 0 || i >= nent)
		return;
	if (!ent[i].fi) {
		/* the account line: check this account */
		long n;
		busy(1);
		mail_err[0] = 0;
		if (!mail_check(ent[i].a, &n) && mail_err[0])
			alert(1, "[1][%s][ OK ]", mail_err);
		busy(0);
		folders_build();
		return;
	}
	folders_select(ent[i].a, ent[i].fi);
}

static int key(WIN *w, short kstate, short k)
{
	short scan = KEY_SCAN(k);
	long i = sel;
	(void)kstate;
	if (scan == 0x48 || scan == 0x50) {	/* up / down: next folder */
		do {
			i += scan == 0x50 ? 1 : -1;
		} while (i >= 0 && i < nent && (!ent[i].fi || ent[i].fi->noselect));
		if (i >= 0 && i < nent) {
			folders_select(ent[i].a, ent[i].fi);
			win_ensure_visible(w, i);
		}
		return 1;
	}
	return 0;
}

void folders_init(void);
void folders_init(void)
{
	w_folders.h = -1;
	w_folders.pane = 1;
	w_folders.draw = draw;
	w_folders.click = click;
	w_folders.key = key;
	strcpy(w_folders.title, "Folders");
}
