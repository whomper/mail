/*
 * DSPTEST - checks EMail's RSA on the DSP56001 (dsp/rsa.a56 through
 * atari/dsprsa.c) against answers worked out in Python, and times it.
 */
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "tos.h"
#include "dsprsa.h"
#include "testvec.h"

unsigned long tos_hz200(void);

static char out[2000];
static int olen;

static void say(const char *fmt, ...)
{
	char line[160];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	Cconws(line);
	if (olen + (int)strlen(line) < (int)sizeof(out)) {
		strcpy(out + olen, line);
		olen += strlen(line);
	}
}

int main(void)
{
	static unsigned char x[512];
	unsigned i;
	int bad = 0;
	say("\033E DSPTEST - RSA on the DSP56001\r\n\r\n");
	if (!dsp_rsa_present()) {
		say("no DSP\r\n");
		bad = 1;
	}
	for (i = 0; i < sizeof(vecs) / sizeof(vecs[0]) && !bad; i++) {
		const VEC *v = &vecs[i];
		size_t len = (size_t)((v->bits + 7) / 8);
		unsigned char e[3];
		unsigned long t0, t;
		int ok;
		e[0] = (unsigned char)(v->e >> 16);
		e[1] = (unsigned char)(v->e >> 8);
		e[2] = (unsigned char)v->e;
		memcpy(x, v->x, len);
		t0 = tos_hz200();
		ok = dsp_rsa_public(x, len, v->n, len, e, 3);
		t = (tos_hz200() - t0) * 5;
		ok = ok && !memcmp(x, v->y, len);
		if (!ok)
			bad++;
		say("%4d bits, e=%-6lu %5lu ms  %s\r\n", v->bits, v->e, t, ok ? "correct" : "WRONG");
	}
	say("\r\n%s\r\n", bad ? "FAILED" : "all correct");
	{
		long h = Fcreate("DSPTEST.TXT", 0);
		if (h >= 0) {
			Fwrite((short)h, olen, out);
			Fclose((short)h);
		}
	}
	return bad;
}
