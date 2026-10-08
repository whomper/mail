/*
 * compose.h - writing mail. The editor holds a message as plain text in
 * the Atari charset, with its header lines on top:
 *
 *   To: Dana <dana@example.com>, avi@example.org
 *   Cc:
 *   Bcc:
 *   Subject: Shalom
 *   Attach: C:\PICS\FALCON.JPG
 *
 *   body...
 *
 * compose_build() turns that into an RFC 5322 message in UTF-8.
 */
#ifndef COMPOSE_H
#define COMPOSE_H

#include "store.h"
#include "mime.h"

/* starting texts for the editor (malloc'ed) */
char *compose_new(ACCOUNT *a, const char *to);
char *compose_reply(ACCOUNT *a, MSG *m, int all);
/* eml: path of the cached original, attached when it has attachments */
char *compose_forward(ACCOUNT *a, MSG *m, const char *eml);

/* build the message; in_reply_to / references may be NULL */
char *compose_build(ACCOUNT *a, const char *text, long len, const char *in_reply_to,
		    const char *references, char *err, int errlen, long *outlen);

/* bare addresses from To, Cc and X-Mail-Bcc of a built message */
int  compose_rcpts(const char *raw, long len, char rcpt[][96], int max);
/* the message without the X-Mail-Bcc line, for sending */
char *compose_for_smtp(const char *raw, long len, long *outlen);

/* split an address list at commas (outside quotes and <>) */
int  addr_list(const char *s, char out[][160], int max);

#endif
