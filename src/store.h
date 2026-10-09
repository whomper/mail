/*
 * store.h - MAIL's data on disk: settings and accounts (MAIL.INF),
 * the folder list of each account, and per folder a header index plus
 * cached message files. Like the GFA Troll, IMAP folders are a mirror
 * of the server (headers always, bodies once read); POP3 accounts keep
 * their mail in local folders. All names are 8.3 so plain TOS copes:
 *
 *   MAIL.INF                  settings and accounts
 *   ADDRESS.TXT                address book, one "Name <a@b>" per line
 *   MAIL\ACCT1\FOLDERS.LST     folders of account 1
 *   MAIL\ACCT1\F1A2B3C4\INDEX.DAT   headers of one folder
 *   MAIL\ACCT1\F1A2B3C4\0000002A.EML  message with UID 42
 */
#ifndef STORE_H
#define STORE_H

#include "imap.h"

#define MAXACCT   8
#define MAXFOLDER 64

typedef struct {
	char server[160];	/* name on the IMAP server (modified UTF-7) */
	char disp[64];		/* shown name, Atari charset, without parents */
	char dir[12];		/* directory under the account */
	short role;		/* FR_* */
	short noselect;
	short depth;		/* hierarchy level for indenting */
	short local;		/* local-only folder (Outbox, POP Inbox...) */
	long total, unread;
} FINFO;

typedef struct {
	char name[32];		/* account label */
	char fullname[64];	/* your name in From: (Atari charset) */
	char email[96];
	short pop;		/* 0 IMAP, 1 POP3 */
	char host[64];
	unsigned short port;
	char user[96], pass[64];
	short leave;		/* POP3: keep mail on the server */
	char smtphost[64];
	unsigned short smtpport;
	char smtpuser[96], smtppass[64];	/* empty user: same as above; "-": no login */
	char sentname[96];	/* IMAP Sent folder override */
	char signature[256];	/* \n separated, Atari charset */
	/* Falcon mode: the provider's own servers, reached with TLS */
	char dhost[64];
	unsigned short dport;
	char dsmtphost[64];
	unsigned short dsmtpport;
	short dsec, dsmtpsec;	/* 0: by the port (993/995/465 TLS, else STARTTLS) */
	/* Falcon mode's own logins, kept apart from the gateway's */
	char duser[96], dpass[64];
	char dsmtpuser[96], dsmtppass[64];

	/* runtime */
	short id;		/* 1..MAXACCT */
	char dir[200];
	short nfolders;
	FINFO folders[MAXFOLDER];
	IMAP *im;		/* kept open while working, like the GFA Troll */
	unsigned long im_used;
	/* the last connection failed: work from the cache until Check mail
	   (or the timed check) connects again */
	short cut;
	char cuterr[160];
} ACCOUNT;

/* where a screen font keeps the Hebrew letters */
#define HEB_ATARI 0		/* the Atari character set, 0xC2-0xDC (TOS, EmuTOS) */
#define HEB_ISO   1		/* ISO-8859-8 / windows-1255 places, 0xE0-0xFA */
#define HEB_DOS   2		/* DOS code page 862, 0x80-0x9A */

typedef struct {
	short tz;		/* minutes east of UTC */
	short check;		/* minutes between automatic checks, 0 = off */
	short page;		/* messages loaded at a time (more on request) */
	short keepcache;	/* keep message bodies on disk when quitting */
	short log;		/* write MAIL.LOG */
	short hebrew;		/* Hebrew keyboard on at start */
	short offline;
	short wrap;		/* compose wrap column */
	short main_x, main_y, main_w, main_h;	/* main window, 0 = not yet placed */
	short pane_w, pane_h;	/* folders pane width, message list height (pixels) */
	short ed_x, ed_y, ed_w, ed_h;		/* the editor window */
	short font_id, font_pt;	/* text font: VDI font id (1 = system) and size */
	short hebfont;		/* where the font has its Hebrew letters: HEB_* */
	short falcon;		/* Falcon mode: TLS on the Atari (and its DSP), no gateway */
	char workdir[200];
} OPTIONS;

typedef struct {
	unsigned long uid;
	unsigned short flags;
	long size;
	unsigned long date;	/* seconds since 1970, UTC */
	char *from, *subject, *to;
} HDR;

typedef struct {
	ACCOUNT *acct;
	FINFO *fi;
	char dir[200];
	unsigned long uidvalidity, uidnext;
	long window;		/* IMAP: how many of the newest messages to mirror */
	long exists;		/* IMAP: messages in the folder on the server */
	HDR *h;
	long n, cap;
	short dirty;
} FOLDER;

extern OPTIONS opt;
extern ACCOUNT *accts[MAXACCT];
extern short naccts;

/* settings */
int  store_init(const char *workdir);	/* loads MAIL.INF (creates dirs) */
int  store_save_settings(void);
ACCOUNT *acct_new(void);
/* the servers in use: the gateway's (plain), or in Falcon mode the
   provider's (TLS). smtp: 0 incoming, 1 outgoing */
const char *acct_host(ACCOUNT *a, int smtp);
unsigned short acct_port(ACCOUNT *a, int smtp);
int acct_sec(ACCOUNT *a, int smtp);		/* SEC_* (conn.h) */
/* fill in a provider's direct servers from the e-mail address; 1 if known */
int acct_preset(ACCOUNT *a);
/* the login in use: the gateway's or, in Falcon mode, Falcon mode's own.
   smtp: 0 incoming, 1 outgoing; NULL user: the server needs no login */
const char *acct_user(ACCOUNT *a, int smtp);
const char *acct_pass(ACCOUNT *a, int smtp);
/* a login set never filled in starts as a copy of the other one */
void acct_fill_logins(ACCOUNT *a);
void acct_delete(ACCOUNT *a);
void acct_dirs(ACCOUNT *a);

/* folders of an account */
int  folders_load(ACCOUNT *a);
int  folders_save(ACCOUNT *a);
FINFO *folder_find(ACCOUNT *a, const char *server);
FINFO *folder_role(ACCOUNT *a, short role);
FINFO *folder_add(ACCOUNT *a, const char *server, char delim, short role, short noselect, short local);
void folder_path(ACCOUNT *a, FINFO *f, char *out, int size);

/* header index of one folder */
FOLDER *fold_open(ACCOUNT *a, FINFO *fi);
int  fold_save(FOLDER *f);
void fold_close(FOLDER *f);
HDR *fold_add(FOLDER *f, unsigned long uid);	/* keeps uid order */
HDR *fold_get(FOLDER *f, unsigned long uid);
void fold_remove(FOLDER *f, unsigned long uid);	/* also its cached body */
void fold_clear(FOLDER *f);
void hdr_set(HDR *h, const char *from, const char *subject, const char *to);
void fold_count(FOLDER *f);		/* local folders: updates fi->total / unread */
/* fill a header from the raw header block of a message */
void hdr_from_raw(HDR *h, const char *raw, long len);

/* message bodies */
void msg_path(FOLDER *f, unsigned long uid, char *out, int size);
int  msg_save(FOLDER *f, unsigned long uid, const char *data, long len);
char *msg_load(FOLDER *f, unsigned long uid, long *len);
void cache_clear_all(void);	/* bodies of IMAP folders (quit without keepcache) */

/* address book */
int  abook_add(const char *addr);	/* "Name <a@b>", skips duplicates */
char *abook_load(long *len);

/* dates for display, in the configured time zone */
void date_str(unsigned long t, char *out, int size, int with_year);

#endif
