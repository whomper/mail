/*
 * draw.c - text output for MAIL's windows. Text is kept in logical
 * order; lines containing Hebrew go through bidi_visual() and, when the
 * paragraph is right-to-left, are aligned to the right edge.
 */
#include <string.h>
#include "ui.h"
#include "../src/bidi.h"

void clip_on(GRECT *r)
{
	short pxy[4];
	pxy[0] = r->x;
	pxy[1] = r->y;
	pxy[2] = r->x + r->w - 1;
	pxy[3] = r->y + r->h - 1;
	vs_clip(vdi_h, 1, pxy);
}

void fill(GRECT *r, short color)
{
	short pxy[4];
	if (r->w <= 0 || r->h <= 0)
		return;
	pxy[0] = r->x;
	pxy[1] = r->y;
	pxy[2] = r->x + r->w - 1;
	pxy[3] = r->y + r->h - 1;
	vsf_interior(vdi_h, 1);
	vsf_color(vdi_h, color);
	vsf_perimeter(vdi_h, 0);
	vswr_mode(vdi_h, 1);
	vr_recfl(vdi_h, pxy);
}

void hline(short x1, short x2, short y)
{
	short pxy[4];
	pxy[0] = x1;
	pxy[1] = y;
	pxy[2] = x2;
	pxy[3] = y;
	vswr_mode(vdi_h, 1);
	vsl_color(vdi_h, 1);
	v_pline(vdi_h, 2, pxy);
}

short line_rtl(const char *s, long n)
{
	if (n > BIDI_MAX)
		n = BIDI_MAX;
	return bidi_has_rtl(s, (short)n) && bidi_is_rtl(s, (short)n);
}

void text_at(short x, short y, const char *s, long n, short cols, short flags)
{
	char vis[BIDI_MAX];
	const char *out = s;
	short i;
	if (cols <= 0)
		return;
	if (n > cols)
		n = cols;
	if (n > BIDI_MAX)
		n = BIDI_MAX;
	if (n <= 0)
		return;
	if (bidi_has_rtl(s, (short)n)) {
		short rtl = (flags & TX_LTR) ? 0 : bidi_is_rtl(s, (short)n);
		bidi_visual(s, (short)n, rtl, vis);
		out = vis;
		if (rtl && (flags & TX_RIGHT))
			x += (cols - (short)n) * cw;
	}
	vswr_mode(vdi_h, 2);
	vst_color(vdi_h, (flags & TX_INVERSE) ? 0 : 1);
	vst_effects(vdi_h, (flags & TX_BOLD) ? 1 : (flags & TX_LIGHT) ? 2 : 0);
	for (i = 0; i < n; i += 120) {
		short k = n - i > 120 ? 120 : (short)(n - i);
		v_gtext_n(vdi_h, x + i * cw, y, out + i, k);
	}
	if (flags & (TX_BOLD | TX_LIGHT))
		vst_effects(vdi_h, 0);
}

long wrap_text(const char *s, long n, short width, long *breaks, long max)
{
	long count = 0, i = 0;
	if (width < 8)
		width = 8;
	while (i <= n && count < max) {
		long start = i, end, sp = -1, j;
		breaks[count++] = start;
		for (j = start; j < n && s[j] != '\n' && j - start < width; j++)
			if (s[j] == ' ')
				sp = j;
		if (j >= n)
			break;
		if (s[j] == '\n') {
			i = j + 1;
			if (i == n)
				break;
			continue;
		}
		/* too long: break after the last space, or hard at width */
		end = (sp > start) ? sp + 1 : j;
		i = end;
	}
	return count;
}
