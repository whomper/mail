/*
 * font.c - the font for the text in MAIL's windows (Options > Font).
 *
 * The system font in its two sizes, and, with a GDOS (NVDI, SpeedoGDOS,
 * FontGDOS...), every monospaced font it has, in a few sizes. Only
 * monospaced fonts: the message list's columns and the Hebrew layout
 * work in character cells. Menus and dialogs keep the system font.
 * Hebrew letters come from the Atari character set, which the system
 * font has; a GDOS font may not.
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"

#define MAXFONT 32

typedef struct {
	short id, pt;		/* id 1 = system: pt 0 screen size, 8 small, 16 large */
	char label[44];
} FONTOPT;

static FONTOPT fonts[MAXFONT];
static short nfonts, gdos_fonts = -1;

/* set the font; cw/ch become its cell size. 0 if it isn't there */
static int set_font(short id, short pt)
{
	short w, h;
	if (id == 1) {
		vst_font(vdi_h, 1);
		vst_height(vdi_h, pt == 8 ? 6 : pt == 16 ? 13 : (gl_hchar >= 16 ? 13 : 6), &w, &h);
	} else {
		if (vst_font(vdi_h, id) != id)
			return 0;
		vst_point(vdi_h, pt, &w, &h);
	}
	if (w < 4 || h < 6)
		return 0;
	cw = w;
	ch = h;
	return 1;
}

void font_apply(void)
{
	if (opt.font_id != 1 && gdos_fonts < 0 && vq_gdos())
		gdos_fonts = vst_load_fonts(vdi_h, 0);
	if (!set_font(opt.font_id, opt.font_pt)) {
		opt.font_id = 1;
		opt.font_pt = 0;
		set_font(1, 0);
	}
	vst_alignment(vdi_h, 0, 5);
}

static void add(short id, short pt, const char *label)
{
	short i;
	if (nfonts >= MAXFONT)
		return;
	for (i = 0; i < nfonts; i++)
		if (fonts[i].id == id && fonts[i].pt == pt)
			return;
	fonts[nfonts].id = id;
	fonts[nfonts].pt = pt;
	strncpy(fonts[nfonts].label, label, sizeof(fonts[0].label) - 1);
	fonts[nfonts].label[sizeof(fonts[0].label) - 1] = 0;
	nfonts++;
}

static void build_list(void)
{
	short i, keep_id = opt.font_id, keep_pt = opt.font_pt;
	nfonts = 0;
	add(1, 0, "System font");
	add(1, 8, "System font, small (8x8)");
	add(1, 16, "System font, large (8x16)");
	if (gdos_fonts < 0)
		gdos_fonts = vq_gdos() ? vst_load_fonts(vdi_h, 0) : 0;
	for (i = 2; i <= gdos_fonts + 1 && nfonts < MAXFONT; i++) {
		static const short sizes[] = { 9, 10, 12 };
		char name[34], label[48];
		short id = vqt_name(vdi_h, i, name), k, n;
		if (id <= 1)
			continue;
		for (n = 31; n >= 0 && name[n] == ' '; n--)	/* names are space padded */
			name[n] = 0;
		for (k = 0; k < 3; k++) {
			short w, h, got;
			if (vst_font(vdi_h, id) != id)
				break;
			got = vst_point(vdi_h, sizes[k], &w, &h);
			/* monospaced only */
			if (vqt_width(vdi_h, 'i') != vqt_width(vdi_h, 'W'))
				break;
			snprintf(label, sizeof(label), "%.30s %d pt", name, got);
			add(id, got, label);
		}
	}
	set_font(keep_id, keep_pt);
}

/* Options > Font */
short dlg_pick_list(const char *title, const char **items, short n);

void font_menu(void)
{
	const char *items[MAXFONT];
	static char shown[MAXFONT][46];
	short i, r;
	build_list();
	for (i = 0; i < nfonts; i++) {
		int cur = fonts[i].id == opt.font_id && fonts[i].pt == opt.font_pt;
		snprintf(shown[i], sizeof(shown[i]), "%s%s", cur ? "\x08 " : "  ", fonts[i].label);
		items[i] = shown[i];
	}
	r = dlg_pick_list(nfonts > 3 ? "Font for the text" : "Font (more with a GDOS)", items, nfonts);
	if (r < 0)
		return;
	opt.font_id = fonts[r].id;
	opt.font_pt = fonts[r].pt;
	font_apply();
	store_save_settings();
	win_relayout();
}
