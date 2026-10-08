/*
 * plat_tos.c - files, folders and time on GEMDOS.
 */
#include <string.h>
#include <stdlib.h>
#include "tos.h"
#include "../src/plat.h"

void (*pf_idle)(void);

int pf_open(const char *path, int mode)
{
	long h;
	if (mode == PF_WRITE)
		return (int)Fcreate(path, 0);
	h = Fopen(path, mode == PF_APPEND ? 2 : 0);
	if (h < 0 && mode == PF_APPEND)
		h = Fcreate(path, 0);
	else if (h >= 0 && mode == PF_APPEND)
		Fseek(0, (short)h, 2);
	return (int)h;
}

long pf_read(int h, void *buf, long n) { return Fread((short)h, n, buf); }
long pf_write(int h, const void *buf, long n) { return Fwrite((short)h, n, buf); }
void pf_close(int h) { Fclose((short)h); }

static DTA dta;

long pf_size(const char *path)
{
	void *old = Fgetdta();
	long r;
	Fsetdta(&dta);
	r = Fsfirst(path, 0x07) == 0 ? (long)dta.d_length : -1;
	Fsetdta(old);
	return r;
}

int pf_exists(const char *path)
{
	void *old = Fgetdta();
	long r;
	Fsetdta(&dta);
	r = Fsfirst(path, 0x17);
	Fsetdta(old);
	return r == 0;
}

int pf_mkdir(const char *path)
{
	long r = Dcreate(path);
	return (r == 0 || r == -36 || pf_exists(path)) ? 0 : (int)r;
}

int pf_remove(const char *path) { return (int)Fdelete(path); }
int pf_rename(const char *from, const char *to) { return (int)Frename(from, to); }

int pf_list(const char *dir, int (*cb)(const char *, long, void *), void *ud)
{
	char pat[200];
	DTA d;
	void *old = Fgetdta();
	long r;
	int n = 0;
	strncpy(pat, dir, sizeof(pat) - 6);
	pat[sizeof(pat) - 6] = 0;
	if (*pat && pat[strlen(pat) - 1] != '\\')
		strcat(pat, "\\");
	strcat(pat, "*.*");
	Fsetdta(&d);
	for (r = Fsfirst(pat, 0x01); r == 0; r = Fsnext()) {
		if (d.d_attrib & 0x18)
			continue;
		n++;
		if (cb(d.d_fname, (long)d.d_length, ud))
			break;
		Fsetdta(&d);	/* the callback may have used the DTA */
	}
	Fsetdta(old);
	return n;
}

void pf_now(PFTIME *t)
{
	unsigned short d = (unsigned short)Tgetdate();
	unsigned short tm = (unsigned short)Tgettime();
	t->year = 1980 + (d >> 9);
	t->mon = (d >> 5) & 15;
	t->day = d & 31;
	t->hour = tm >> 11;
	t->min = (tm >> 5) & 63;
	t->sec = (tm & 31) * 2;
}

unsigned long pf_ms(void)
{
	return tos_hz200() * 5;
}

void pf_debug(const char *s)
{
	nf_debug(s);
	nf_debug("\n");
}
