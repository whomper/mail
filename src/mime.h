/*
 * mime.h - reading internet messages: headers, MIME structure, the
 * readable text (plain text preferred, HTML turned into text) and the
 * attachments. All text comes out in the Atari character set.
 */
#ifndef MIME_H
#define MIME_H

#define MIME_MAXPARTS 32

enum { ENC_7BIT, ENC_BASE64, ENC_QP };

typedef struct {
	char *name;		/* file name (Atari charset), never NULL */
	char *type;		/* "image/jpeg" */
	long off, len;		/* encoded body inside the raw message */
	short enc;		/* ENC_* */
	long size;		/* approximate decoded size */
} MIMEPART;

typedef struct {
	char *from, *to, *cc, *reply_to, *subject, *date;	/* decoded */
	char *message_id, *references, *in_reply_to, *list_post;	/* raw */
	char *text;		/* body to show, lines separated by \n */
	long textlen;
	short html;		/* the text came from HTML */
	short nparts;
	MIMEPART parts[MIME_MAXPARTS];
} MSG;

MSG  *mime_parse(const char *raw, long len);
void  mime_free(MSG *m);
/* decoded bytes of an attachment, malloc'ed */
char *mime_part_data(const char *raw, const MIMEPART *p, long *outlen);

/* the unfolded value of header `name` in the header block hdr[0..n), malloc'ed, or NULL */
char *mime_header(const char *hdr, long n, const char *name);
/* a parameter (charset, boundary, name...) of a structured header value */
char *mime_param(const char *value, const char *param);
/* length of the header block (up to and including the blank line) */
long  mime_header_len(const char *raw, long len);

/* Atari text of an HTML document */
char *html_to_text(const char *html, long n, long *outlen);

/* "Name <a@b>" -> a@b and Name parts (Atari text), into caller buffers */
void  addr_split(const char *addr, char *name, int nsize, char *email, int esize);

/* RFC 2822 date -> seconds since 1970 UTC (0 if unreadable) */
unsigned long mime_date(const char *s);

#endif
