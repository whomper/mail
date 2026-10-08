/*
 * reader.c - the message window: headers, attachments (click one to
 * save it) and the text, word-wrapped to the window. Hebrew paragraphs
 * are laid out right to left; quoted lines are drawn light.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "../src/mail.h"
#include "../src/util.h"
#include "../src/plat.h"

MSG *cur_msg;
char *cur_raw;
long cur_rawlen;
unsigned long cur_uid;

enum { L_BODY, L_HEAD, L_ATTACH, L_RULE };

typedef struct {
	const char *s;
	long n;
	short kind, attach, label, rtl;
} LINE;

static char *head;		/* header lines, \n separated */
static char *body;		/* text with tabs expanded */
static long bodylen;
static LINE *lines;
static long nlines;

static void free_layout(void)
{
	free(lines);
	lines = 0;
	nlines = 0;
	w_reader.total = 0;
}

void reader_clear(void)
{
	free_layout();
	mime_free(cur_msg);
	cur_msg = 0;
	free(cur_raw);
	cur_raw = 0;
	free(head);
	head = 0;
	free(body);
	body = 0;
	cur_uid = 0;
	w_reader.top = 0;
	win_title(&w_reader, " Message ");
	win_info(&w_reader, "");
	win_sliders(&w_reader);
	win_redraw(&w_reader, 0);
}

static void add_line(const char *s, long n, short kind, short attach, short label)
{
	LINE *l = &lines[nlines++];
	l->s = s;
	l->n = n;
	l->kind = kind;
	l->attach = attach;
	l->label = label;
	l->rtl = kind == L_BODY ? line_rtl(s, n) : 0;
}

/* lay the message out for the current window width */
static void layout(void)
{
	short width = w_reader.work.w / cw - 1;
	long hl = head ? strlen(head) : 0, max, *br, nb, i;
	const char *p;
	free_layout();
	if (!cur_msg)
		return;
	if (width < 20)
		width = 20;
	max = bodylen / 8 + hl / 8 + cur_msg->nparts + 64;
	br = malloc(max * sizeof(long));
	lines = malloc((max + 8) * sizeof(LINE));
	if (!br || !lines) {
		free(br);
		free_layout();
		return;
	}
	/* header lines: "Label: value", wrapped with an indent */
	for (p = head; p && *p; ) {
		const char *e = strchr(p, '\n');
		long n = e ? e - p : (long)strlen(p);
		const char *colon = memchr(p, ':', n);
		short label = colon ? (short)(colon - p + 1) : 0;
		nb = wrap_text(p, n, width, br, max - nlines - 8);
		for (i = 0; i < nb; i++) {
			long s = br[i], end = i + 1 < nb ? br[i + 1] : n;
			add_line(p + s, end - s, L_HEAD, 0, i == 0 ? label : 0);
		}
		p = e ? e + 1 : p + n;
	}
	for (i = 0; i < cur_msg->nparts; i++)
		add_line(cur_msg->parts[i].name, strlen(cur_msg->parts[i].name), L_ATTACH, (short)i, 0);
	add_line("", 0, L_RULE, 0, 0);
	nb = wrap_text(body, bodylen, width, br, max - nlines - 2);
	for (i = 0; i < nb; i++) {
		long s = br[i], end = i + 1 < nb ? br[i + 1] : bodylen;
		while (end > s && (body[end - 1] == '\n' || body[end - 1] == ' '))
			end--;
		add_line(body + s, end - s, L_BODY, 0, 0);
	}
	free(br);
	w_reader.total = nlines;
	win_sliders(&w_reader);
}

static void resized(WIN *w)
{
	(void)w;
	layout();
}

static char *expand_tabs(const char *s, long n, long *outlen)
{
	SBUF b;
	long i, col = 0;
	sb_init(&b);
	for (i = 0; i < n; i++) {
		if (s[i] == '\t') {
			do
				sb_addc(&b, ' ');
			while (++col % 8);
		} else {
			sb_addc(&b, s[i]);
			col = s[i] == '\n' ? 0 : col + 1;
		}
	}
	*outlen = b.len;
	return sb_steal(&b);
}

void reader_show(HDR *h)
{
	long len;
	char *raw;
	SBUF hb;
	if (!cur_folder || !h)
		return;
	busy(1);
	mail_err[0] = 0;
	raw = mail_fetch(cur_folder, h, &len);
	busy(0);
	if (!raw) {
		alert(1, "[1][%s][ OK ]", mail_err[0] ? mail_err : "Can't read this message.");
		return;
	}
	reader_clear();
	cur_raw = raw;
	cur_rawlen = len;
	cur_uid = h->uid;
	cur_msg = mime_parse(raw, len);
	if (!cur_msg) {
		alert(1, "[1][Not enough memory to show|this message.][ OK ]");
		return;
	}
	sb_init(&hb);
	sb_printf(&hb, "From: %s\n", cur_msg->from);
	if (cur_msg->to[0])
		sb_printf(&hb, "To: %s\n", cur_msg->to);
	if (cur_msg->cc[0])
		sb_printf(&hb, "Cc: %s\n", cur_msg->cc);
	if (h->date) {
		char d[24];
		date_str(h->date, d, sizeof(d), 1);
		sb_printf(&hb, "Date: %s\n", d);
	}
	sb_adds(&hb, "Subject: ");
	sb_adds(&hb, cur_msg->subject);
	sb_adds(&hb, "\n");
	head = sb_steal(&hb);
	body = expand_tabs(cur_msg->text, cur_msg->textlen, &bodylen);
	{
		char t[100];
		snprintf(t, sizeof(t), " %.90s ", cur_msg->subject[0] ? cur_msg->subject : "(no subject)");
		win_title(&w_reader, t);
		if (cur_msg->html)
			win_info(&w_reader, " Shown as text (the message is HTML)");
		else if (cur_msg->nparts)
			win_info(&w_reader, " Click an attachment to save it");
		else
			win_info(&w_reader, "");
	}
	if (w_reader.h <= 0)
		win_open(&w_reader);
	w_reader.top = 0;
	layout();
	win_redraw(&w_reader, 0);
}

static void draw(WIN *w, GRECT *clip)
{
	long i, rows = win_rows(w) + 1;
	short cols = w->work.w / cw - 1, x0 = w->work.x + cw / 2;
	fill(clip, 0);
	if (!cur_msg) {
		text_at(x0, w->work.y + ch, "No message selected.", 20, cols, TX_LIGHT);
		return;
	}
	for (i = w->top; i < nlines && i < w->top + rows; i++) {
		LINE *l = &lines[i];
		short y = w->work.y + (short)((i - w->top) * ch);
		switch (l->kind) {
		case L_HEAD:
			if (l->label) {
				text_at(x0, y, l->s, l->label, cols, TX_BOLD);
				text_at(x0 + l->label * cw, y, l->s + l->label, l->n - l->label, cols - l->label, 0);
			} else {
				text_at(x0, y, l->s, l->n, cols, 0);
			}
			break;
		case L_ATTACH: {
			char t[140], sz[16];
			MIMEPART *p = &cur_msg->parts[l->attach];
			if (p->size < 1024)
				snprintf(sz, sizeof(sz), "%ld bytes", p->size);
			else
				snprintf(sz, sizeof(sz), "%ld KB", (p->size + 512) / 1024);
			snprintf(t, sizeof(t), "\xAF %s  (%s, %s)", p->name, p->type, sz);
			text_at(x0, y, t, strlen(t), cols, TX_BOLD);
			break;
		}
		case L_RULE:
			hline(w->work.x, w->work.x + w->work.w - 1, y + ch / 2);
			break;
		default:
			text_at(x0, y, l->s, l->n, cols,
				TX_RIGHT | ((l->n && l->s[0] == '>') ? TX_LIGHT : 0));
		}
	}
}

/* an 8.3 name for saving: the ASCII letters of the original */
static void short_name(const char *name, char *out)
{
	const char *dot = strrchr(name, '.');
	short n = 0, e = 0;
	const char *p;
	for (p = name; *p && p != dot && n < 8; p++) {
		unsigned char c = (unsigned char)*p;
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
			out[n++] = (char)c;
		else if (c >= 'a' && c <= 'z')
			out[n++] = (char)(c - 32);
	}
	if (!n) {
		strcpy(out, "ATTACH");
		n = 6;
	}
	if (dot) {
		out[n++] = '.';
		for (p = dot + 1; *p && e < 3; p++) {
			unsigned char c = (unsigned char)*p;
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
				out[n++] = (char)(c >= 'a' ? c - 32 : c);
				e++;
			}
		}
	}
	out[n] = 0;
}

void reader_save_attachment(short i)
{
	static char path[160], name[16];
	char full[200], *data;
	short button;
	long len;
	if (!cur_msg || i < 0 || i >= cur_msg->nparts)
		return;
	if (!path[0]) {
		str_copy(path, opt.workdir, sizeof(path) - 5);
		strcat(path, "\\*.*");
	}
	short_name(cur_msg->parts[i].name, name);
	if (!fsel_exinput(path, name, &button, "Save attachment as") || !button || !name[0])
		return;
	str_copy(full, path, sizeof(full));
	{
		char *bs = strrchr(full, '\\');
		if (bs)
			bs[1] = 0;
	}
	strncat(full, name, sizeof(full) - strlen(full) - 1);
	data = mime_part_data(cur_raw, &cur_msg->parts[i], &len);
	if (!data || pf_save(full, data, len) < 0)
		alert(1, "[1][Can't write|%s][ OK ]", full);
	free(data);
}

static void click(WIN *w, short mx, short my, short clicks, short kstate)
{
	long i = w->top + (my - w->work.y) / ch;
	(void)mx;
	(void)clicks;
	(void)kstate;
	if (i >= 0 && i < nlines && lines[i].kind == L_ATTACH)
		reader_save_attachment(lines[i].attach);
}

static int key(WIN *w, short kstate, short k)
{
	long rows = win_rows(w);
	switch (KEY_SCAN(k)) {
	case 0x48: win_scroll_to(w, w->top - ((kstate & 3) ? rows : 1)); return 1;
	case 0x50: win_scroll_to(w, w->top + ((kstate & 3) ? rows : 1)); return 1;
	case 0x39: win_scroll_to(w, w->top + rows - 1); return 1;	/* space */
	case 0x0e: win_scroll_to(w, w->top - rows + 1); return 1;	/* backspace */
	case 0x47: win_scroll_to(w, (kstate & 3) ? w->total : 0); return 1;
	}
	return 0;
}

void reader_init(void);
void reader_init(void)
{
	w_reader.h = -1;
	w_reader.draw = draw;
	w_reader.click = click;
	w_reader.key = key;
	w_reader.resized = resized;
	strcpy(w_reader.title, " Message ");
}
