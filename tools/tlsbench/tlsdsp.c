/*
 * TLSDSP - how fast the Falcon's DSP56001 does the big-number arithmetic
 * of a TLS handshake: Montgomery multiplication of 2048-bit numbers (RSA)
 * and 256-bit numbers (X25519, P-256). Checks the answers too.
 */
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "tos.h"
#include "dspvec.h"

long get_cookie(long id);

#define HZ200   (*(volatile unsigned long *)0x4baL)
#define PSGSEL  (*(volatile unsigned char *)0xffff8800L)
#define PSGWR   (*(volatile unsigned char *)0xffff8802L)
#define HISR    (*(volatile unsigned char *)0xffffa202L)
#define HTXH    (*(volatile unsigned char *)0xffffa205L)
#define HTXM    (*(volatile unsigned char *)0xffffa206L)
#define HTXL    (*(volatile unsigned char *)0xffffa207L)

static char out[3000];
static int olen;

static void say(const char *fmt, ...)
{
	char line[200];
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

static int timeout;

static void put(unsigned long w)
{
	unsigned long t0 = HZ200;
	while (!(HISR & 2))
		if (HZ200 - t0 > 400) { timeout = 1; return; }
	HTXH = (unsigned char)(w >> 16);
	HTXM = (unsigned char)(w >> 8);
	HTXL = (unsigned char)w;
}

static unsigned long get(void)
{
	unsigned long t0 = HZ200, w;
	while (!(HISR & 1))
		if (HZ200 - t0 > 6000) { timeout = 1; return 0; }
	w = (unsigned long)HTXH << 16;
	w |= (unsigned long)HTXM << 8;
	w |= HTXL;
	return w;
}

/* reset the DSP and load the program through its bootstrap */
static void sup_boot(void)
{
	unsigned char v;
	volatile int d;
	int i;
	PSGSEL = 14;
	v = PSGSEL;
	PSGWR = v | 0x10;
	for (d = 0; d < 2000; d++)
		;
	PSGWR = v & ~0x10;
	for (d = 0; d < 2000; d++)
		;
	for (i = 0; i < 512; i++)
		put(dsp_prog[i]);
}

static int vn, vk, ok;
static const unsigned long *va, *vb, *vm, *vt;
static unsigned long vmp, ticks;

static void sup_run(void)
{
	int i;
	unsigned long t0;
	put(vn);
	put(vmp);
	put(vk);
	for (i = 0; i < vn; i++)
		put(va[i]);
	for (i = 0; i < vn; i++)
		put(vb[i]);
	for (i = 0; i < vn - 1; i++)
		put(vm[i]);
	t0 = HZ200;
	put(vm[vn - 1]);		/* the DSP starts now */
	ok = 1;
	for (i = 0; i <= vn; i++) {
		unsigned long w = get();
		if (i == 0)
			ticks = HZ200 - t0;
		if ((w & 0x7fffff) != vt[i])
			ok = 0;
	}
	if (timeout)
		ok = 0;
}

static unsigned long run(int bits, int k)
{
	vk = k;
	if (bits == 2048) {
		vn = V2048_N; vmp = V2048_MP; va = v2048_a; vb = v2048_b; vm = v2048_m; vt = v2048_t;
	} else {
		vn = V256_N; vmp = V256_MP; va = v256_a; vb = v256_b; vm = v256_m; vt = v256_t;
	}
	Supexec(sup_run);
	return ticks;
}

int main(void)
{
	unsigned long t, mm2048_us, mm256_us;
	long mch = get_cookie(0x5F4D4348L), snd = get_cookie(0x5F534E44L);

	say("\033E TLSDSP - TLS arithmetic on the Falcon's DSP56001\r\n");
	say(" _MCH %08lx  _SND %08lx\r\n\r\n", mch, snd);
	if (!(snd & 8)) {
		say("No DSP here (_SND bit 3).\r\nPress a key.\r\n");
		Bconin(2);
		return 0;
	}
	Supexec(sup_boot);
	if (timeout) {
		say("The DSP did not take the program.\r\n");
		goto end;
	}
	run(2048, 1);
	say("2048-bit Montgomery multiply, result %s\r\n", ok ? "correct" : "WRONG");
	run(256, 1);
	say("256-bit Montgomery multiply, result %s\r\n", ok ? "correct" : "WRONG");
	if (timeout)
		goto end;

	t = run(2048, 100);
	mm2048_us = t * 5000 / 100;
	say("\r\n2048-bit multiply: %lu.%02lu ms each (%s)\r\n", mm2048_us / 1000, mm2048_us % 1000 / 10, ok ? "ok" : "WRONG");
	t = run(256, 2000);
	mm256_us = t * 5000 / 2000;
	say("256-bit multiply:  %lu us each (%s)\r\n", mm256_us, ok ? "ok" : "WRONG");

	say("\r\nEstimates with the DSP doing the arithmetic:\r\n");
	say("  RSA-2048 signature check (19 multiplies): %lu ms\r\n", 19 * mm2048_us / 1000);
	say("  X25519 (about 2550 multiplies):           %lu ms\r\n", 2550 * mm256_us / 1000);
	say("  Handshake (2 x X25519 + 3 RSA checks):    %lu ms\r\n",
	    (2 * 2550 * mm256_us + 3 * 19 * mm2048_us) / 1000);
end:
	{
		long h = Fcreate("TLSDSP.TXT", 0);
		if (h >= 0) {
			Fwrite((short)h, olen, out);
			Fclose((short)h);
		}
	}
	say("Saved in TLSDSP.TXT. Press a key.\r\n");
	Bconin(2);
	return 0;
}
