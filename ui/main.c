/*
 * main.c - MAIL: start-up, the menu bar, the event loop and the
 * commands behind menus and keys.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "../src/mail.h"
#include "../src/compose.h"
#include "../src/conn.h"
#include "../src/plat.h"
#include "../src/util.h"

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
short gl_wchar, gl_hchar;
short ap_id;
short hebrew_kbd;
static short quit;
static unsigned long last_check;

void folders_init(void);
void list_init(void);
void reader_init(void);
void editor_init(void);

/* ---------------- menu ---------------- */


typedef struct {
	const char *text;
	short cmd;
} MITEM;

static const MITEM m_desk[] = {
	{ "  About MAIL...   ", C_ABOUT }, { "--------------------", C_SEP },
	{ "  Desk Accessory 1  ", C_ACC }, { "  Desk Accessory 2  ", C_ACC },
	{ "  Desk Accessory 3  ", C_ACC }, { "  Desk Accessory 4  ", C_ACC },
	{ "  Desk Accessory 5  ", C_ACC }, { "  Desk Accessory 6  ", C_ACC }, { 0, 0 }
};
static const MITEM m_file[] = {
	{ "  New message     ^N ", C_NEW }, { "  Check mail      ^K ", C_CHECK },
	{ "  Send Outbox        ", C_SENDQ }, { "---------------------", C_SEP },
	{ "  Save attachment... ", C_SAVEATT }, { "---------------------", C_SEP },
	{ "  Quit            ^Q ", C_QUIT }, { 0, 0 }
};
static const MITEM m_msg[] = {
	{ "  Reply            ^R ", C_REPLY }, { "  Reply to all     ^E ", C_REPLYALL },
	{ "  Forward          ^F ", C_FORWARD }, { "----------------------", C_SEP },
	{ "  Mark as unread   ^U ", C_UNREAD }, { "  Flag             ^G ", C_FLAG },
	{ "  Move to...       ^M ", C_MOVE }, { "  Delete          Del ", C_DELETE },
	{ "----------------------", C_SEP },
	{ "  Send now         ^S ", C_SEND }, { "  Put in Outbox       ", C_SAVEOUT },
	{ "  Attach file...   ^T ", C_ATTACH }, { "  Address book...  ^B ", C_ABOOK }, { 0, 0 }
};
static const MITEM m_fold[] = {
	{ "  Show folders        ", C_FOLDERS }, { "  Refresh folder list ", C_REFRESH },
	{ "  New folder...       ", C_NEWFOLDER }, { "  Delete folder...    ", C_DELFOLDER }, { 0, 0 }
};
static const MITEM m_opts[] = {
	{ "  Accounts...         ", C_ACCOUNTS }, { "  Settings...         ", C_SETTINGS },
	{ "  Font...             ", C_FONT },
	{ "----------------------", C_SEP }, { "  Work offline        ", C_OFFLINE },
	{ "  Falcon mode (TLS)   ", C_FALCON },
	{ "  Hebrew keyboard F10 ", C_HEBREW }, { "  Protocol log        ", C_LOG }, { 0, 0 }
};

static const char *titles[] = { " MAIL ", " File ", " Message ", " Folder ", " Options " };
static const MITEM *drops[] = { m_desk, m_file, m_msg, m_fold, m_opts };
#define NTITLES 5

#define MMAX 64
static OBJECT menu[MMAX];
static short menu_cmd[MMAX];

static short find_item(short cmd)
{
	short i;
	for (i = 0; i < MMAX; i++)
		if (menu_cmd[i] == cmd)
			return i;
	return -1;
}

static void menu_build(void)
{
	short n = 0, t, i, cols = scr_w / gl_wchar, x = 0;
	short first_title, first_drop, screen, prev_drop = -1;
	memset(menu, 0, sizeof(menu));
	/* 0 root, 1 bar, 2 active (titles), titles, screen, drops */
	menu[0].ob_type = G_IBOX;
	menu[0].ob_next = -1;
	menu[0].ob_head = 1;
	menu[0].ob_width = cols;
	menu[0].ob_height = scr_h / gl_hchar;
	menu[1].ob_type = G_BOX;
	menu[1].ob_spec = 0x1100L;
	menu[1].ob_head = menu[1].ob_tail = 2;
	menu[1].ob_width = cols;
	menu[1].ob_height = 0x201;
	menu[2].ob_type = G_IBOX;
	menu[2].ob_next = 1;
	menu[2].ob_x = 2;
	menu[2].ob_height = 0x301;
	n = 3;
	first_title = n;
	for (t = 0; t < NTITLES; t++) {
		OBJECT *o = &menu[n];
		o->ob_type = G_TITLE;
		o->ob_spec = (long)titles[t];
		o->ob_head = o->ob_tail = -1;
		o->ob_x = x;
		o->ob_width = (short)strlen(titles[t]);
		o->ob_height = 0x301;
		o->ob_next = t + 1 < NTITLES ? n + 1 : 2;
		x += o->ob_width;
		n++;
	}
	menu[2].ob_head = first_title;
	menu[2].ob_tail = n - 1;
	menu[2].ob_width = x;
	screen = n++;
	menu[1].ob_next = screen;
	menu[0].ob_tail = screen;
	menu[screen].ob_type = G_IBOX;
	menu[screen].ob_next = 0;
	menu[screen].ob_y = 0x301;
	menu[screen].ob_width = cols;
	menu[screen].ob_height = 19;
	first_drop = n;
	x = 2;
	for (t = 0; t < NTITLES; t++) {
		const MITEM *it = drops[t];
		short d = n++, k = 0, w = 0, dx = x + menu[first_title + t].ob_x;
		for (i = 0; it[i].text; i++)
			if ((short)strlen(it[i].text) > w)
				w = (short)strlen(it[i].text);
		if (dx + w > cols)
			dx = cols - w;
		menu[d].ob_type = G_BOX;
		menu[d].ob_spec = 0xFF1100L;
		menu[d].ob_x = t == 0 ? x : dx;
		menu[d].ob_width = w;
		menu[d].ob_head = n;
		if (prev_drop >= 0)
			menu[prev_drop].ob_next = d;
		prev_drop = d;
		for (i = 0; it[i].text; i++, k++) {
			OBJECT *o = &menu[n];
			o->ob_type = G_STRING;
			o->ob_spec = (long)it[i].text;
			o->ob_head = o->ob_tail = -1;
			o->ob_y = k;
			o->ob_width = w;
			o->ob_height = 1;
			o->ob_state = it[i].cmd == C_SEP ? DISABLED : 0;
			o->ob_next = it[i + 1].text ? n + 1 : d;
			menu_cmd[n] = it[i].cmd;
			n++;
		}
		menu[d].ob_tail = n - 1;
		menu[d].ob_height = k;
	}
	menu[prev_drop].ob_next = screen;
	menu[screen].ob_head = first_drop;
	menu[screen].ob_tail = prev_drop;
	menu[n - 1].ob_flags |= LASTOB;
	for (i = 0; i < n; i++)
		rsrc_obfix(menu, i);
}

static void enable(short cmd, int on)
{
	short i = find_item(cmd);
	if (i >= 0)
		menu_ienable(menu, i, on ? 1 : 0);
}

void menu_update(void)
{
	int msg = cur_msg != 0, ed = w_editor.h > 0;
	enable(C_REPLY, msg);
	enable(C_REPLYALL, msg);
	enable(C_FORWARD, msg);
	enable(C_UNREAD, msg);
	enable(C_FLAG, msg);
	enable(C_MOVE, msg);
	enable(C_DELETE, msg);
	enable(C_SAVEATT, msg && cur_msg->nparts > 0);
	enable(C_SEND, ed);
	enable(C_SAVEOUT, ed);
	enable(C_ATTACH, ed);
	enable(C_ABOOK, ed);
	enable(C_NEWFOLDER, cur_acct && !cur_acct->pop);
	enable(C_DELFOLDER, cur_finfo && !cur_finfo->local && !cur_finfo->role);
	menu_icheck(menu, find_item(C_OFFLINE), opt.offline);
	menu_icheck(menu, find_item(C_FALCON), opt.falcon);
	menu_icheck(menu, find_item(C_HEBREW), hebrew_kbd);
	menu_icheck(menu, find_item(C_LOG), opt.log);
}

/* ---------------- status and helpers ---------------- */

void status(const char *msg)
{
	char tmp[120];
	snprintf(tmp, sizeof(tmp), " %s", msg);
	main_status(*msg ? tmp : "");
}

void busy(int on)
{
	graf_mouse(on ? BUSYBEE : ARROW, 0);
}

/* keep the system breathing while we wait for the network */
static void idle(void)
{
	evnt_timer_(5);
}

static void set_logging(void)
{
	if (opt.log)
		path_join(conn_logfile, sizeof(conn_logfile), opt.workdir, "MAIL.LOG");
	else
		conn_logfile[0] = 0;
}

/* Israeli SI-1452 layout by key position (scancode 0x10-0x35), giving
 * Atari character codes; 0 = key not remapped. From Claude ST. */
static const u8 hebrew_keys[0x36 - 0x10] = {
	/* 10 q..p */ '/', '\'', 0xD4, 0xD5, 0xC2, 0xCA, 0xC7, 0xD8, 0xDA, 0xD2,
	/* 1a [ ] ret ctrl */ 0, 0, 0, 0,
	/* 1e a..' */ 0xD6, 0xC5, 0xC4, 0xCC, 0xD1, 0xCB, 0xC9, 0xCD, 0xD9, 0xDB, ',',
	/* 29 ` lshift \ */ 0, 0, 0,
	/* 2c z../ */ 0xC8, 0xD0, 0xC3, 0xC6, 0xCF, 0xCE, 0xD3, 0xD7, 0xDC, '.'
};

unsigned char key_char(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	unsigned char ascii = KEY_ASCII(key);
	if (hebrew_kbd && !(kstate & (K_LSHIFT | K_RSHIFT | K_CTRL | K_ALT)) &&
	    scan >= 0x10 && scan < 0x36 && hebrew_keys[scan - 0x10])
		ascii = hebrew_keys[scan - 0x10];
	return ascii;
}

void set_hebrew_kbd(short on)
{
	hebrew_kbd = on;
	menu_update();
	if (w_editor.h > 0) {
		/* the editor shows the keyboard in its info line */
		char *i = hebrew_kbd ? " Hebrew keyboard (F10)   ^S send   Esc close"
				     : " ^S send   ^T attach file   F10 Hebrew   Esc close";
		win_info(&w_editor, i);
	}
}

/* folder lists can be rebuilt by the server: find the open one again */
static void refind_current(const char *server)
{
	FOLDER *keep = cur_folder;
	if (!cur_acct)
		return;
	cur_finfo = server[0] ? folder_find(cur_acct, server) : 0;
	if (keep) {
		keep->fi = cur_finfo;
		if (!cur_finfo) {
			fold_close(keep);
			cur_folder = 0;
			reader_clear();
			list_load();
		}
	}
}

/* ---------------- commands ---------------- */

void cmd_check_all(void)
{
	short i;
	long total = 0;
	char server[160], errs[200] = "";
	str_copy(server, cur_finfo ? cur_finfo->server : "", sizeof(server));
	if (opt.offline) {
		alert(1, "[1][MAIL is working offline.|Switch it off in the Options|menu to check mail.][ OK ]");
		return;
	}
	busy(1);
	for (i = 0; i < naccts; i++) {
		long n = 0;
		mail_err[0] = 0;
		if (!mail_check(accts[i], &n) && mail_err[0] && !errs[0])
			str_copy(errs, mail_err, sizeof(errs));
		total += n;
	}
	busy(0);
	refind_current(server);
	folders_build();
	if (cur_folder && cur_finfo) {
		fold_close(cur_folder);
		cur_folder = fold_open(cur_acct, cur_finfo);
		list_refresh();
	}
	last_check = pf_ms();
	if (errs[0])
		alert(1, "[1][%s][ OK ]", errs);
	{
		char m[60];
		snprintf(m, sizeof(m), total == 1 ? "1 new message" : "%ld new messages", total);
		status(m);
	}
	menu_update();
}

static ACCOUNT *account_for_new(void)
{
	if (cur_acct)
		return cur_acct;
	return dlg_pick_account("Write from which account?");
}

static void cmd_reply(int all, int forward)
{
	ACCOUNT *a = cur_acct;
	char *text, refs[1100];
	if (!cur_msg || !a)
		return;
	if (forward) {
		char path[220];
		msg_path(cur_folder, cur_uid, path, sizeof(path));
		text = compose_forward(a, cur_msg, pf_exists(path) ? path : 0);
		editor_open(a, text, 0, 0);
		return;
	}
	text = compose_reply(a, cur_msg, all);
	snprintf(refs, sizeof(refs), "%s%s%s", cur_msg->references,
		 cur_msg->references[0] ? " " : "", cur_msg->message_id);
	abook_add(cur_msg->reply_to[0] ? cur_msg->reply_to : cur_msg->from);
	editor_open(a, text, cur_msg->message_id, refs);
}

static void cmd_delete(void)
{
	HDR *h;
	if (!cur_folder || !(h = fold_get(cur_folder, cur_uid)))
		return;
	if (cur_finfo->role == FR_TRASH &&
	    alert(2, "[2][Delete this message|for good?][Delete|Cancel]") != 1)
		return;
	busy(1);
	mail_err[0] = 0;
	if (!mail_delete(cur_folder, h)) {
		busy(0);
		alert(1, "[1][%s][ OK ]", mail_err);
		return;
	}
	busy(0);
	reader_clear();
	list_after_remove();
	menu_update();
}

static void cmd_move(void)
{
	HDR *h;
	FINFO *dest;
	char server[160];
	if (!cur_folder || !(h = fold_get(cur_folder, cur_uid)))
		return;
	dest = dlg_pick_folder(cur_acct, "Move the message to");
	if (!dest)
		return;
	str_copy(server, dest->server, sizeof(server));
	busy(1);
	mail_err[0] = 0;
	if (!mail_move(cur_folder, h, dest)) {
		busy(0);
		alert(1, "[1][%s][ OK ]", mail_err);
		return;
	}
	busy(0);
	reader_clear();
	list_after_remove();
	menu_update();
}

static void cmd_flag(unsigned short flag, int add)
{
	HDR *h;
	if (!cur_folder || !(h = fold_get(cur_folder, cur_uid)))
		return;
	if (flag == MF_FLAGGED)
		add = !(h->flags & MF_FLAGGED);
	busy(1);
	mail_err[0] = 0;
	if (!mail_flag(cur_folder, h, flag, add))
		alert(1, "[1][%s][ OK ]", mail_err);
	busy(0);
	list_refresh();
	folders_build();
}

static void cmd_save_attachment(void)
{
	short i;
	if (!cur_msg || !cur_msg->nparts)
		return;
	if (cur_msg->nparts == 1) {
		reader_save_attachment(0);
		return;
	}
	for (i = 0; i < cur_msg->nparts; i++) {
		short b = alert(1, "[2][Save the attachment|%s?][Save|Skip|Stop]", cur_msg->parts[i].name);
		if (b == 3)
			break;
		if (b == 1)
			reader_save_attachment(i);
	}
}

static void cmd_accounts(void)
{
	ACCOUNT *a;
	short r;
	if (naccts) {
		short b = alert(1, "[2][Accounts][Edit|New|Cancel]");
		if (b == 3)
			return;
		a = b == 1 ? dlg_pick_account("Which account?") : acct_new();
	} else {
		a = acct_new();
	}
	if (!a) {
		if (naccts >= MAXACCT)
			alert(1, "[1][MAIL has room for %d accounts.][ OK ]", MAXACCT);
		return;
	}
	r = (short)dlg_account(a);
	if (r < 0 || (r == 0 && !a->host[0])) {
		/* deleted, or a new account cancelled */
		if (cur_acct == a) {
			if (cur_folder)
				fold_close(cur_folder);
			cur_folder = 0;
			cur_finfo = 0;
			cur_acct = 0;
			reader_clear();
			list_load();
		}
		acct_delete(a);
	} else if (r > 0) {
		mail_disconnect(a);
		folders_load(a);
	}
	store_save_settings();
	folders_build();
	menu_update();
	if (r > 0 && !opt.offline &&
	    alert(1, "[2][Check this account now?][Check|Later]") == 1) {
		long n;
		busy(1);
		mail_err[0] = 0;
		if (!mail_check(a, &n) && mail_err[0])
			alert(1, "[1][%s][ OK ]", mail_err);
		busy(0);
		folders_build();
	}
}

void cmd_new_folder(ACCOUNT *a)
{
	char name[48] = "";
	if (!a || a->pop)
		return;
	if (!dlg_ask("New folder", "Name:", name, 40))
		return;
	{
		char server[160];
		str_copy(server, cur_finfo ? cur_finfo->server : "", sizeof(server));
		busy(1);
		mail_err[0] = 0;
		if (!mail_folder_create(a, name))
			alert(1, "[1][%s][ OK ]", mail_err);
		busy(0);
		refind_current(server);
	}
	folders_build();
}

void cmd_delete_folder(ACCOUNT *a, FINFO *fi)
{
	char server[160];
	if (!a || !fi || fi->local || fi->role)
		return;
	if (alert(2, "[2][Delete the folder|\"%s\"|and all its messages|on the server?][Delete|Cancel]",
		  fi->disp) != 1)
		return;
	str_copy(server, cur_finfo && cur_finfo != fi ? cur_finfo->server : "", sizeof(server));
	if (fi == cur_finfo) {
		if (cur_folder)
			fold_close(cur_folder);
		cur_folder = 0;
		cur_finfo = 0;
		reader_clear();
		list_load();
	}
	busy(1);
	mail_err[0] = 0;
	if (!mail_folder_delete(a, fi))
		alert(1, "[1][%s][ OK ]", mail_err);
	busy(0);
	if (cur_acct == a)
		refind_current(server);
	folders_build();
	menu_update();
}

/* edit one account (from its right-click menu) */
void cmd_edit_account(ACCOUNT *a)
{
	short r = (short)dlg_account(a);
	if (r < 0) {
		if (cur_acct == a) {
			if (cur_folder)
				fold_close(cur_folder);
			cur_folder = 0;
			cur_finfo = 0;
			cur_acct = 0;
			reader_clear();
			list_load();
		}
		acct_delete(a);
	} else if (r > 0) {
		mail_disconnect(a);
	}
	store_save_settings();
	folders_build();
	menu_update();
}

void cmd_refresh_folders(ACCOUNT *a)
{
	char server[160];
	str_copy(server, cur_finfo ? cur_finfo->server : "", sizeof(server));
	busy(1);
	mail_err[0] = 0;
	if (!mail_refresh_folders(a) && mail_err[0])
		alert(1, "[1][%s][ OK ]", mail_err);
	busy(0);
	if (cur_acct == a)
		refind_current(server);
	folders_build();
}

static void wait_release(void)
{
	short mx, my, mb, ks;
	for (;;) {
		graf_mkstate(&mx, &my, &mb, &ks);
		if (!(mb & 3))
			return;
		evnt_timer_(10);
	}
}

/* the "any button" event can't count clicks: see whether this press
   follows the last one closely enough to be a double click */
static short double_click(short mx, short my)
{
	static unsigned long last;
	static short lx, ly;
	unsigned long now = pf_ms();
	short n = (now - last < 400 && mx - lx < 4 && lx - mx < 4 && my - ly < 4 && ly - my < 4) ? 2 : 1;
	last = n == 2 ? 0 : now;
	lx = mx;
	ly = my;
	return n;
}

static void do_quit(void)
{
	if (editor_dirty() &&
	    alert(2, "[2][You are still writing a message.|Quit anyway?][Quit|Cancel]") != 1)
		return;
	quit = 1;
}

/* closing the main window ends MAIL */
void main_closed(void);
void main_closed(void)
{
	do_quit();
}

static void command(short cmd);
void ui_command(short cmd)
{
	command(cmd);
}

static void command(short cmd)
{
	switch (cmd) {
	case C_ABOUT: dlg_about(); break;
	case C_NEW: {
		ACCOUNT *a = account_for_new();
		if (a)
			editor_open(a, compose_new(a, ""), 0, 0);
		break;
	}
	case C_CHECK: cmd_check_all(); break;
	case C_SENDQ: {
		short i;
		busy(1);
		for (i = 0; i < naccts; i++) {
			int sent;
			mail_err[0] = 0;
			if (mail_send_outbox(accts[i], &sent) < 0 && mail_err[0]) {
				busy(0);
				alert(1, "[1][%s][ OK ]", mail_err);
				busy(1);
			}
		}
		busy(0);
		folders_build();
		break;
	}
	case C_SAVEATT: cmd_save_attachment(); break;
	case C_QUIT: do_quit(); break;
	case C_REPLY: cmd_reply(0, 0); break;
	case C_REPLYALL: cmd_reply(1, 0); break;
	case C_FORWARD: cmd_reply(0, 1); break;
	case C_UNREAD: cmd_flag(MF_SEEN, 0); break;
	case C_MARKREAD: cmd_flag(MF_SEEN, 1); break;
	case C_FLAG: cmd_flag(MF_FLAGGED, 1); break;
	case C_MOVE: cmd_move(); break;
	case C_DELETE: cmd_delete(); break;
	case C_SEND: editor_send(1); break;
	case C_SAVEOUT: editor_send(0); break;
	case C_ATTACH: editor_attach(); break;
	case C_ABOOK: editor_insert_address(); break;
	case C_FOLDERS: main_open(); break;
	case C_REFRESH: {
		short i;
		char server[160];
		str_copy(server, cur_finfo ? cur_finfo->server : "", sizeof(server));
		busy(1);
		for (i = 0; i < naccts; i++) {
			mail_err[0] = 0;
			if (!mail_refresh_folders(accts[i]) && mail_err[0]) {
				busy(0);
				alert(1, "[1][%s][ OK ]", mail_err);
				busy(1);
			}
		}
		busy(0);
		refind_current(server);
		folders_build();
		break;
	}
	case C_NEWFOLDER: cmd_new_folder(cur_acct); break;
	case C_DELFOLDER: cmd_delete_folder(cur_acct, cur_finfo); break;
	case C_ACCOUNTS: cmd_accounts(); break;
	case C_SETTINGS:
		dlg_falcon = opt.falcon;
		if (dlg_settings()) {
			if (dlg_falcon != opt.falcon)
				falcon_mode_set(dlg_falcon);
			set_logging();
			store_save_settings();
			if (w_editor.h <= 0)
				hebrew_kbd = opt.hebrew;
			list_refresh();
			win_redraw(&w_reader, 0);
		}
		break;
	case C_FALCON:
		falcon_mode_set(!opt.falcon);
		list_titles();
		break;
	case C_OFFLINE:
		opt.offline = !opt.offline;
		if (opt.offline)
			mail_disconnect_all();
		store_save_settings();
		list_titles();
		break;
	case C_HEBREW: set_hebrew_kbd(!hebrew_kbd); break;
	case C_FONT: font_menu(); break;
	case C_LOG:
		opt.log = !opt.log;
		set_logging();
		store_save_settings();
		break;
	}
	menu_update();
}

/* ---------------- Falcon mode ----------------
 * On: MAIL talks TLS to the providers itself (BearSSL on the 68030, RSA
 * signatures on the DSP). Off: plain, through the Raspberry Pi gateway.
 * Each account keeps both sets of servers. */
short dlg_falcon;

int falcon_mode_set(int on)
{
	if (on) {
		char missing[200];
		int i;
		if (get_cookie(0x5F435055L) < 20) {	/* _CPU */
			alert(1, "[3][Falcon mode needs a 68020 or|better (Falcon, TT): this|Atari is too slow for TLS.][ OK ]");
			return 0;
		}
		if (!pf_exists(conn_cacert)) {
			alert(1, "[3][Falcon mode needs the root|certificates: put CACERT.PEM|next to MAIL.PRG. Get it|from curl.se/docs/caextract|or from MAIL's GitHub page.][ OK ]");
			return 0;
		}
		missing[0] = 0;
		for (i = 0; i < naccts; i++) {
			ACCOUNT *a = accts[i];
			if (!a->dhost[0] && !acct_preset(a)) {
				long l = (long)strlen(missing);
				snprintf(missing + l, sizeof(missing) - l, "%s%s", l ? ", " : "", a->name);
			}
		}
		if (missing[0])
			alert(1, "[1][Falcon mode connects to the|providers directly. Enter the|servers for: %s|in Options > Accounts.][ OK ]", missing);
	}
	opt.falcon = (short)on;
	falcon_dsp_init();
	mail_disconnect_all();
	store_save_settings();
	menu_update();
	return 1;
}

/* ---------------- events ---------------- */

static int shortcut(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	WIN *top = win_topmost();
	if (scan == 0x44) {		/* F10 */
		set_hebrew_kbd(!hebrew_kbd);
		return 1;
	}
	if (scan == 0x62) {		/* Help */
		alert(1, "[1][^N new  ^K check mail  ^R reply|^E reply all  ^F forward  ^U unread|"
			 "^G flag  ^M move  Del delete|^S send  ^T attach  ^B addresses|F10 Hebrew keyboard  ^Q quit][ OK ]");
		return 1;
	}
	if (kstate & K_CTRL) {
		short cmd = C_NONE;
		switch (scan) {
		case 0x31: cmd = C_NEW; break;		/* N */
		case 0x25: cmd = C_CHECK; break;	/* K */
		case 0x10: cmd = C_QUIT; break;		/* Q */
		case 0x13: cmd = C_REPLY; break;	/* R */
		case 0x12: cmd = C_REPLYALL; break;	/* E */
		case 0x21: cmd = C_FORWARD; break;	/* F */
		case 0x16: cmd = C_UNREAD; break;	/* U */
		case 0x22: cmd = C_FLAG; break;		/* G */
		case 0x32: cmd = C_MOVE; break;		/* M */
		case 0x1f: cmd = C_SEND; break;		/* S */
		case 0x14: cmd = C_ATTACH; break;	/* T */
		case 0x30: cmd = C_ABOOK; break;	/* B */
		}
		if (cmd == C_SEND || cmd == C_ATTACH || cmd == C_ABOOK) {
			if (w_editor.h <= 0)
				return 1;
		} else if ((cmd == C_REPLY || cmd == C_REPLYALL || cmd == C_FORWARD || cmd == C_UNREAD ||
			    cmd == C_FLAG || cmd == C_MOVE) && !cur_msg) {
			return 1;
		}
		if (cmd != C_NONE) {
			command(cmd);
			return 1;
		}
	}
	/* Tab moves the keyboard to the next pane */
	if (scan == 0x0f && top && top->pane) {
		win_focus(top == &w_folders ? &w_list : top == &w_list ? &w_reader : &w_folders);
		return 1;
	}
	/* Delete removes the message unless the editor has the keyboard */
	if (scan == 0x53 && top != &w_editor && cur_msg) {
		command(C_DELETE);
		return 1;
	}
	return 0;
}

static void place_windows(void)
{
	/* the editor where it was last time, if that still fits */
	w_editor.place.x = opt.ed_x;
	w_editor.place.y = opt.ed_y;
	w_editor.place.w = opt.ed_w;
	w_editor.place.h = opt.ed_h;
	if (opt.ed_w < 30 * cw || opt.ed_h < 8 * ch || opt.ed_x < 0 || opt.ed_y < desk_y ||
	    opt.ed_x + opt.ed_w > desk_x + desk_w + 4 || opt.ed_y + opt.ed_h > desk_y + desk_h + 4) {
		w_editor.place.x = desk_x + desk_w / 10;
		w_editor.place.y = desk_y + ch;
		w_editor.place.w = desk_w - desk_w / 5;
		w_editor.place.h = desk_h - 2 * ch;
	}
}

/* the folder MAIL.PRG was started from */
static void work_dir(char *out, int size)
{
	char path[160];
	short drv = (short)Dgetdrv();
	path[0] = 0;
	Dgetpath(path, 0);
	snprintf(out, size, "%c:%s", 'A' + drv, path);
	if (out[strlen(out) - 1] == '\\')
		out[strlen(out) - 1] = 0;
}

int main(void)
{
	short work_in_dummy, work_out[57], msg[8], d;
	char dir[200];
	EVENT ev;

	ap_id = appl_init();
	if (ap_id < 0)
		return 1;
	vdi_h = graf_handle(&gl_wchar, &gl_hchar, &d, &d);
	cw = gl_wchar;
	ch = gl_hchar;
	(void)work_in_dummy;
	vdi_h = v_opnvwk_(vdi_h, work_out);
	scr_w = work_out[0] + 1;
	scr_h = work_out[1] + 1;
	vq_extnd(vdi_h, 1, work_out);
	planes = work_out[4];
	vst_alignment(vdi_h, 0, 5);
	wind_get(0, WF_WORKXYWH, &desk_x, &desk_y, &desk_w, &desk_h);
	graf_mouse(BUSYBEE, 0);

	work_dir(dir, sizeof(dir));
	store_init(dir);
	falcon_dsp_init();
	font_apply();
	hebrew_kbd = opt.hebrew;
	set_logging();
	net_init();
	pf_idle = idle;
	mail_status = status;
	for (d = 0; d < naccts; d++)
		folders_load(accts[d]);

	folders_init();
	list_init();
	reader_init();
	editor_init();
	place_windows();
	menu_build();
	menu_bar(menu, 1);
	menu_update();
	graf_mouse(ARROW, 0);

	main_open();
	folders_build();
	list_load();
	reader_clear();

	if (!naccts) {
		alert(1, "[1][Welcome to MAIL!||Let's set up your mail account.][ OK ]");
		cmd_accounts();
	} else {
		/* check mail at start, then open the first inbox */
		FINFO *in;
		if (!opt.offline)
			cmd_check_all();
		in = folder_role(accts[0], FR_INBOX);
		if (in)
			folders_select(accts[0], in);
	}
	last_check = pf_ms();
	evnt_set_m1(1, 0, 0, 1, 1);

	while (!quit) {
		/* 0x101/3/0: wake on any button press, left or right */
		short which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER | MU_M1, 0x101, 3, 0,
					  1000, msg, &ev);
		if (which & MU_M1)
			win_hover(ev.mx, ev.my);
		if (which & MU_MESAG) {
			switch (msg[0]) {
			case MN_SELECTED:
				command(menu_cmd[msg[4]]);
				menu_tnormal(menu, msg[3], 1);
				break;
			case AP_TERM:
				quit = 1;
				break;
			default:
				win_message(msg);
			}
		}
		if (which & MU_KEYBD) {
			WIN *top = win_topmost();
			if (!shortcut(ev.kstate, ev.kreturn) && top && top->key)
				top->key(top, ev.kstate, ev.kreturn);
		}
		if (which & MU_BUTTON) {
			short h = wind_find(ev.mx, ev.my), t, d;
			wind_get(0, WF_TOP, &t, &d, &d, &d);
			if (h > 0 && h != t)	/* a click in a window behind: bring it up too */
				wind_set(h, WF_TOP, 0, 0, 0, 0);
			if (win_mouse(ev.mx, ev.my, ev.mbutton, double_click(ev.mx, ev.my), ev.kstate))
				menu_update();
			wait_release();
		}
		if ((which & MU_TIMER) && opt.check > 0 && !opt.offline && naccts &&
		    pf_ms() - last_check > (unsigned long)opt.check * 60000UL)
			cmd_check_all();
	}

	mail_disconnect_all();
	if (cur_folder)
		fold_close(cur_folder);
	if (!opt.keepcache)
		cache_clear_all();
	for (d = 0; d < naccts; d++)
		folders_save(accts[d]);
	store_save_settings();
	win_close(&w_editor);
	main_close();
	store_save_settings();
	menu_bar(menu, 0);
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
