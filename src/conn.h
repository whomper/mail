/*
 * conn.h - a buffered, line-oriented TCP connection with timeouts and
 * an optional protocol log (MAIL.LOG), shared by IMAP, POP3 and SMTP.
 */
#ifndef CONN_H
#define CONN_H

#include "util.h"

typedef struct {
	int h;
	char buf[4096];
	long pos, len;
	long timeout_ms;
	char err[160];
	const char *name;		/* "IMAP", "POP3", "SMTP" for the log */
} CONN;

CONN *conn_open(const char *name, const char *host, unsigned short port, char *err, int errlen);
void  conn_close(CONN *c);
int   conn_write(CONN *c, const char *data, long n);
/* send one command line (CRLF added); secret: hide arguments in the log */
int   conn_cmd(CONN *c, int secret, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
/* read one line without CRLF into b (b is reset first); -1 on error */
long  conn_getline(CONN *c, SBUF *b);
/* read exactly n bytes and append them to b */
int   conn_getbytes(CONN *c, SBUF *b, long n);

/* logging: set conn_logfile to a path to append the dialogue to it */
extern char conn_logfile[200];
void conn_log(const char *who, const char *dir, const char *text, long n);

/* progress for the UI: bytes received so far in the current operation */
extern long conn_bytes;

#endif
