/*
 * smtp.h - SMTP submission client (RFC 5321) with AUTH PLAIN/LOGIN.
 */
#ifndef SMTP_H
#define SMTP_H

#include "conn.h"

typedef struct {
	CONN *c;
	char err[200];
	char ext[400];		/* EHLO keywords, upper case */
	SBUF line;
} SMTP;

/* user may be empty for servers (or a gateway) that need no login */
SMTP *smtp_open(const char *host, unsigned short port, int sec, const char *helo,
		const char *user, const char *pass, char *err, int errlen);
/* rcpts: n bare addresses */
int   smtp_send(SMTP *s, const char *from, const char **rcpts, int n,
		const char *data, long len);
void  smtp_quit(SMTP *s);

#endif
