/*
 * mail.h - what the user can do, independent of the GEM interface:
 * check mail, mirror IMAP folders, read, flag, delete, move, send.
 */
#ifndef MAIL_H
#define MAIL_H

#include "store.h"

extern char mail_err[200];
/* the last call failed because EMail works offline or the account is not
   connected (see ACCOUNT.cut), not because of the message or folder */
extern int mail_unreachable;
/* the UI shows progress here (may be NULL) */
extern void (*mail_status)(const char *msg);

int  mail_connect(ACCOUNT *a);			/* IMAP: log in or reuse */
void mail_disconnect(ACCOUNT *a);
void mail_disconnect_all(void);

int  mail_refresh_folders(ACCOUNT *a);		/* IMAP LIST */
int  mail_sync_folder(ACCOUNT *a, FINFO *fi, long *newmsgs);
/* mirror opt.page more (older) messages of an IMAP folder */
int  mail_load_more(ACCOUNT *a, FINFO *fi, long *added);
/* inbox (IMAP sync or POP3 download), then send the Outbox */
int  mail_check(ACCOUNT *a, long *newmsgs);

/* the raw message: from the cache, else downloaded; marks it read */
char *mail_fetch(FOLDER *f, HDR *h, long *len);
int  mail_flag(FOLDER *f, HDR *h, unsigned short flags, int add);
/* read/flag changes not yet on the server, and sending them (one STORE
   per flag); 1 when sent or when they must wait for a connection */
int  mail_flags_pending(FOLDER *f);
int  mail_push_flags(FOLDER *f);
int  mail_delete(FOLDER *f, HDR *h);
int  mail_mark_all_read(ACCOUNT *a, FINFO *fi, FOLDER *open);	/* the whole folder, on the server too; open: the
   folder if the screen has it open (it is changed itself), or NULL */
int  mail_move(FOLDER *f, HDR *h, FINFO *dest);

int  mail_queue(ACCOUNT *a, const char *raw, long len);	/* into the Outbox */
int  mail_send_outbox(ACCOUNT *a, int *sent);

int  mail_folder_create(ACCOUNT *a, const char *name);	/* Atari charset */
int  mail_folder_delete(ACCOUNT *a, FINFO *fi);

#endif
