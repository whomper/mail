/*
 * mail_cli.c - drives MAIL's mail core from a terminal, for the
 * integration tests (tests/integration.sh). Not part of the Atari
 * program; text is printed as UTF-8.
 *
 *   mail-cli DIR check N            check account N (1-based)
 *   mail-cli DIR folders N          list folders
 *   mail-cli DIR sync N FOLDER      mirror one IMAP folder
 *   mail-cli DIR list N FOLDER      headers in the local index
 *   mail-cli DIR show N FOLDER UID  read a message
 *   mail-cli DIR send N FILE        FILE is editor text in UTF-8
 *   mail-cli DIR delete N FOLDER UID
 *   mail-cli DIR move N FOLDER UID DEST
 *   mail-cli DIR flag N FOLDER UID +seen|-seen|+flagged
 *   mail-cli DIR mkdir N NAME       NAME in UTF-8
 *   mail-cli DIR rmdir N FOLDER
 *   mail-cli DIR clearcache
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "store.h"
#include "mail.h"
#include "mime.h"
#include "compose.h"
#include "charset.h"
#include "util.h"

static void say(const char *atari)
{
	char *u = atari_to_utf8(atari, strlen(atari), 0);
	fputs(u, stdout);
	free(u);
}

static void st(const char *m)
{
	if (*m && getenv("MAIL_DEBUG"))
		fprintf(stderr, "[%s]\n", m);
}

static FINFO *find_folder(ACCOUNT *a, const char *name)
{
	short i;
	for (i = 0; i < a->nfolders; i++)
		if (!strcmp(a->folders[i].server, name))
			return &a->folders[i];
	for (i = 0; i < a->nfolders; i++)
		if (!strcasecmp(a->folders[i].disp, name))
			return &a->folders[i];
	return 0;
}

static int fail(const char *what)
{
	fprintf(stderr, "error: %s: %s\n", what, mail_err);
	return 1;
}

int main(int argc, char **argv)
{
	ACCOUNT *a = 0;
	const char *cmd;
	int r = 0;

	if (argc < 3) {
		fprintf(stderr, "usage: mail-cli DIR command ...\n");
		return 2;
	}
	store_init(argv[1]);
	if (getenv("MAIL_LOG")) {
		path_join(conn_logfile, sizeof(conn_logfile), argv[1], "MAIL.LOG");
	}
	net_init();
	mail_status = st;
	cmd = argv[2];
	if (argc > 3) {
		int n = atoi(argv[3]);
		if (n < 1 || n > naccts) {
			fprintf(stderr, "no account %d\n", n);
			return 2;
		}
		a = accts[n - 1];
		folders_load(a);
	}

	if (!strcmp(cmd, "check") && a) {
		long n = 0;
		if (!mail_check(a, &n))
			return fail("check");
		printf("new: %ld\n", n);
	} else if (!strcmp(cmd, "folders") && a) {
		short i;
		if (!a->pop && !mail_refresh_folders(a))
			return fail("folders");
		for (i = 0; i < a->nfolders; i++) {
			FINFO *f = &a->folders[i];
			printf("%-24s role=%d local=%d noselect=%d depth=%d total=%ld unread=%ld disp=", f->server,
			       f->role, f->local, f->noselect, f->depth, f->total, f->unread);
			say(f->disp);
			printf("\n");
		}
	} else if (!strcmp(cmd, "sync") && a && argc > 4) {
		FINFO *fi = find_folder(a, argv[4]);
		long n;
		if (!fi && !mail_refresh_folders(a))
			return fail("folders");
		fi = find_folder(a, argv[4]);
		if (!fi) {
			fprintf(stderr, "no folder %s\n", argv[4]);
			return 1;
		}
		if (!mail_sync_folder(a, fi, &n))
			return fail("sync");
		printf("synced: %ld total, %ld unread, %ld new\n", fi->total, fi->unread, n);
	} else if (!strcmp(cmd, "list") && a && argc > 4) {
		FINFO *fi = find_folder(a, argv[4]);
		FOLDER *f;
		long i;
		if (!fi) {
			fprintf(stderr, "no folder %s\n", argv[4]);
			return 1;
		}
		f = fold_open(a, fi);
		for (i = 0; i < f->n; i++) {
			HDR *h = &f->h[i];
			char d[32];
			date_str(h->date, d, sizeof(d), 1);
			printf("%lu\t%c%c%c\t%ld\t%s\t", h->uid, h->flags & MF_SEEN ? ' ' : 'N',
			       h->flags & MF_FLAGGED ? '!' : ' ', h->flags & MF_ATTACH ? '@' : ' ', h->size, d);
			say(h->from);
			printf("\t");
			say(h->subject);
			printf("\n");
		}
		fold_close(f);
	} else if (!strcmp(cmd, "show") && a && argc > 5) {
		FINFO *fi = find_folder(a, argv[4]);
		FOLDER *f;
		HDR *h;
		char *raw;
		long len;
		MSG *m;
		short i;
		if (!fi)
			return 1;
		f = fold_open(a, fi);
		h = fold_get(f, strtoul(argv[5], 0, 10));
		if (!h) {
			fprintf(stderr, "no message %s\n", argv[5]);
			return 1;
		}
		raw = mail_fetch(f, h, &len);
		if (!raw)
			return fail("fetch");
		m = mime_parse(raw, len);
		printf("From: ");
		say(m->from);
		printf("\nTo: ");
		say(m->to);
		printf("\nSubject: ");
		say(m->subject);
		printf("\nDate: ");
		say(m->date);
		printf("\n");
		for (i = 0; i < m->nparts; i++) {
			printf("Attachment: ");
			say(m->parts[i].name);
			printf(" (%s, %ld bytes)\n", m->parts[i].type, m->parts[i].size);
		}
		printf("\n");
		say(m->text);
		printf("\n");
		mime_free(m);
		free(raw);
		fold_close(f);
	} else if (!strcmp(cmd, "send") && a && argc > 4) {
		long len, al, rl;
		char *u = pf_load(argv[4], &len), *at, *raw, err[200];
		int sent = 0;
		if (!u)
			return 1;
		at = cs_to_atari(u, len, CS_UTF8, &al);
		raw = compose_build(a, at, al, 0, 0, err, sizeof(err), &rl);
		if (!raw) {
			fprintf(stderr, "error: %s\n", err);
			return 1;
		}
		if (!mail_queue(a, raw, rl))
			return fail("queue");
		if (mail_send_outbox(a, &sent) < 0)
			return fail("send");
		printf("sent: %d\n", sent);
		free(u);
		free(at);
		free(raw);
	} else if ((!strcmp(cmd, "delete") || !strcmp(cmd, "move") || !strcmp(cmd, "flag")) && a && argc > 5) {
		FINFO *fi = find_folder(a, argv[4]);
		FOLDER *f;
		HDR *h;
		if (!fi)
			return 1;
		f = fold_open(a, fi);
		h = fold_get(f, strtoul(argv[5], 0, 10));
		if (!h) {
			fprintf(stderr, "no message %s\n", argv[5]);
			return 1;
		}
		if (!strcmp(cmd, "delete"))
			r = mail_delete(f, h);
		else if (!strcmp(cmd, "move") && argc > 6)
			r = find_folder(a, argv[6]) ? mail_move(f, h, find_folder(a, argv[6])) : 0;
		else if (argc > 6) {
			unsigned short fl = strstr(argv[6], "seen") ? MF_SEEN : MF_FLAGGED;
			r = mail_flag(f, h, fl, argv[6][0] == '+');
		}
		fold_close(f);
		folders_save(a);
		if (!r)
			return fail(cmd);
		printf("ok\n");
		r = 0;
	} else if (!strcmp(cmd, "mkdir") && a && argc > 4) {
		char *at = cs_to_atari(argv[4], strlen(argv[4]), CS_UTF8, 0);
		if (!mail_folder_create(a, at))
			return fail("mkdir");
		printf("ok\n");
		free(at);
	} else if (!strcmp(cmd, "rmdir") && a && argc > 4) {
		FINFO *fi = find_folder(a, argv[4]);
		if (!fi || !mail_folder_delete(a, fi))
			return fail("rmdir");
		printf("ok\n");
	} else if (!strcmp(cmd, "clearcache")) {
		short i;
		for (i = 0; i < naccts; i++)
			folders_load(accts[i]);
		cache_clear_all();
		printf("ok\n");
	} else {
		fprintf(stderr, "bad command\n");
		r = 2;
	}
	mail_disconnect_all();
	return r;
}
