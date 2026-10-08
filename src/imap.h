/*
 * imap.h - IMAP4rev1 client (RFC 3501) with the extensions a modern
 * server offers and the GFA Troll uses: UIDPLUS, MOVE, SPECIAL-USE, NAMESPACE.
 */
#ifndef IMAP_H
#define IMAP_H

#include "conn.h"

/* message flags (also used by the local cache) */
#define MF_SEEN      0x0001
#define MF_ANSWERED  0x0002
#define MF_FLAGGED   0x0004
#define MF_DELETED   0x0008
#define MF_DRAFT     0x0010
#define MF_FORWARDED 0x0020
#define MF_JUNK      0x0040
#define MF_ATTACH    0x0080	/* multipart/mixed: probably has attachments */
#define MF_CACHED    0x0100	/* body is in the local cache */

/* folder roles */
#define FR_INBOX  1
#define FR_SENT   2
#define FR_TRASH  3
#define FR_DRAFTS 4
#define FR_JUNK   5
#define FR_ARCHIVE 6
#define FR_OUTBOX 7	/* local only */

typedef struct {
	CONN *c;
	unsigned short tag;
	char caps[600];
	char err[200];
	char delim;
	unsigned long exists, uidvalidity, uidnext;
	char selected[200];
	short readonly;
	SBUF resp;		/* the current response, literals inlined */
} IMAP;

typedef struct {
	unsigned long seq, uid, size;
	unsigned short flags;
	char internaldate[40];
	const char *hdr;	/* BODY[HEADER...] contents */
	long hdrlen;
	const char *body;	/* BODY[] contents */
	long bodylen;
} IMAPFETCH;

IMAP *imap_login(const char *host, unsigned short port, const char *user,
		 const char *pass, char *err, int errlen);
void  imap_logout(IMAP *im);
int   imap_has(IMAP *im, const char *cap);

/* LIST "" "*": cb(name as on the server, delimiter, role, noselect) */
int imap_list(IMAP *im, void (*cb)(void *ud, const char *name, char delim, int role, int noselect), void *ud);
int imap_select(IMAP *im, const char *mbox, int readonly);
/* UID FETCH set items; cb for every FETCH response */
int imap_fetch(IMAP *im, const char *uidset, const char *items,
	       void (*cb)(void *ud, IMAPFETCH *f), void *ud);
int imap_store(IMAP *im, const char *uidset, int add, unsigned short flags);
int imap_copy(IMAP *im, const char *uidset, const char *dest);
int imap_move(IMAP *im, const char *uidset, const char *dest);	/* MOVE or COPY+delete */
int imap_expunge(IMAP *im, const char *uidset);
int imap_append(IMAP *im, const char *mbox, unsigned short flags, const char *data, long len);
int imap_create(IMAP *im, const char *mbox);
int imap_delete(IMAP *im, const char *mbox);
int imap_noop(IMAP *im);

/* "\"name\"" with escapes, into out */
void imap_quote(char *out, int size, const char *s);
unsigned short imap_parse_flags(const char *list, long n);

#endif
