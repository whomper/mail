/*
 * TLSBENCH - how long the pieces of a TLS handshake take on this Atari.
 * BearSSL 0.6, built for the 68030. Prints the times and writes
 * TLSBENCH.TXT next to the program.
 */
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "tos.h"
#include "bearssl.h"

unsigned long tos_hz200(void);
long get_cookie(long id);

static char out[4096];
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

/* run f until at least 2 seconds have passed (but at least once);
   milliseconds per call */
static unsigned long timeit(void (*f)(void), int min_runs)
{
	unsigned long t0 = tos_hz200(), t;
	int n = 0;
	do {
		f();
		n++;
		t = tos_hz200() - t0;
	} while (n < min_runs || t < 400);
	return t * 5 / n;
}

static unsigned char scalar[32] = {
	0x77,0x07,0x6d,0x0a,0x73,0x18,0xa5,0x7d,0x3c,0x16,0xc1,0x72,0x51,0xb2,0x66,0x45,
	0xdf,0x4c,0x2f,0x87,0xeb,0xc0,0x99,0x2a,0xb1,0x77,0xfb,0xa5,0x1d,0xb9,0x2c,0x2a };
static unsigned char pt[65];
static size_t ptlen;
static const br_ec_impl *ec;
static int curve;

static void ec_base(void)
{
	unsigned char r[65];
	ec->mulgen(r, scalar, 32, curve);
}

static void ec_mul(void)
{
	unsigned char r[65];
	memcpy(r, pt, ptlen);
	ec->mul(r, ptlen, scalar, 32, curve);
}

/* ECDSA */
static unsigned char hash[32], sig[64];
static unsigned char pubpt[65];
static br_ec_public_key pubk;
static br_ec_private_key privk;
static br_ecdsa_vrfy vrfy;

static void ecdsa_verify(void)
{
	if (!vrfy(ec, hash, 32, &pubk, sig, 64))
		say("  (signature check failed!)\r\n");
}

/* RSA-2048 public operation, e = 65537 (what a signature check does) */
static unsigned char rsa_n[256], rsa_x[256], rsa_e[3] = { 1, 0, 1 };
static br_rsa_public_key rsak;
static br_rsa_public rsapub;

static void rsa_verify(void)
{
	unsigned char x[256];
	memcpy(x, rsa_x, 256);
	rsapub(x, 256, &rsak);
}

/* bulk data */
static unsigned char buf[4096], key[32], iv[12];

static void sha256_4k(void)
{
	br_sha256_context c;
	unsigned char h[32];
	br_sha256_init(&c);
	br_sha256_update(&c, buf, sizeof(buf));
	br_sha256_out(&c, h);
}

static br_aes_ct_ctr_keys aes_ct;
static br_aes_big_ctr_keys aes_big;
static br_aes_small_ctr_keys aes_small;
static br_ghash gh;

static void gcm_ct_4k(void)
{
	unsigned char y[16];
	br_aes_ct_ctr_run(&aes_ct, iv, 1, buf, sizeof(buf));
	memset(y, 0, 16);
	gh(y, key, buf, sizeof(buf));
}
static void gcm_big_4k(void)
{
	unsigned char y[16];
	br_aes_big_ctr_run(&aes_big, iv, 1, buf, sizeof(buf));
	memset(y, 0, 16);
	gh(y, key, buf, sizeof(buf));
}
static void aes_big_4k(void)
{
	br_aes_big_ctr_run(&aes_big, iv, 1, buf, sizeof(buf));
}
static void aes_small_4k(void)
{
	br_aes_small_ctr_run(&aes_small, iv, 1, buf, sizeof(buf));
}
static void ghash_4k(void)
{
	unsigned char y[16];
	memset(y, 0, 16);
	gh(y, key, buf, sizeof(buf));
}
static void chapoly_4k(void)
{
	unsigned char tag[16];
	br_poly1305_ctmul32_run(key, iv, buf, sizeof(buf), 0, 0, tag, br_chacha20_ct_run, 1);
}

static unsigned long kbs(unsigned long ms)
{
	return ms ? 4000UL / ms : 0;	/* 4 KB per call -> KB/s */
}

static unsigned long ms_ec(const br_ec_impl *impl, int crv, int base)
{
	ec = impl;
	curve = crv;
	if (base)
		return timeit(ec_base, 1);
	return timeit(ec_mul, 1);
}

int main(void)
{
	long cpu = get_cookie(0x5F435055L), fpu = get_cookie(0x5F465055L), mch = get_cookie(0x5F4D4348L);
	unsigned long x25519, p256, ecdsa, rsa, best_sig;
	int i;

	say("\033E TLSBENCH - TLS handshake pieces on this Atari (BearSSL 0.6)\r\n");
	say(" _CPU %ld  _FPU %08lx  _MCH %08lx\r\n\r\n", cpu, fpu, mch);
	for (i = 0; i < (int)sizeof(buf); i++)
		buf[i] = (unsigned char)(i * 7);
	for (i = 0; i < 32; i++)
		key[i] = (unsigned char)(i + 1);

	/* X25519: generate our key, then the shared secret */
	ptlen = 32;
	memset(pt, 0, 32);
	pt[0] = 9;
	say("X25519 (key exchange; a handshake does 2):\r\n");
	x25519 = ms_ec(&br_ec_c25519_m31, BR_EC_curve25519, 0);
	say("  m31 %5lu ms\r\n", x25519);
	{
		unsigned long t;
		t = ms_ec(&br_ec_c25519_i31, BR_EC_curve25519, 0);
		say("  i31 %5lu ms\r\n", t);
		if (t < x25519) x25519 = t;
		t = ms_ec(&br_ec_c25519_m15, BR_EC_curve25519, 0);
		say("  m15 %5lu ms\r\n", t);
		if (t < x25519) x25519 = t;
		t = ms_ec(&br_ec_c25519_i15, BR_EC_curve25519, 0);
		say("  i15 %5lu ms\r\n", t);
		if (t < x25519) x25519 = t;
	}

	/* P-256 */
	say("P-256 point multiply (ECDHE on P-256):\r\n");
	ptlen = br_ec_p256_m31.mulgen(pt, scalar, 32, BR_EC_secp256r1);
	p256 = ms_ec(&br_ec_p256_m31, BR_EC_secp256r1, 0);
	say("  m31 %5lu ms\r\n", p256);
	{
		unsigned long t = ms_ec(&br_ec_p256_m15, BR_EC_secp256r1, 0);
		say("  m15 %5lu ms\r\n", t);
		if (t < p256) p256 = t;
		t = ms_ec(&br_ec_prime_i31, BR_EC_secp256r1, 0);
		say("  i31 %5lu ms\r\n", t);
		if (t < p256) p256 = t;
	}

	/* ECDSA P-256 verify */
	say("ECDSA P-256 signature check:\r\n");
	{
		static unsigned char priv[32];
		br_ec_impl const *e = &br_ec_p256_m31;
		size_t sl;
		memcpy(priv, scalar, 32);
		priv[0] &= 0x7f;
		privk.curve = BR_EC_secp256r1;
		privk.x = priv;
		privk.xlen = 32;
		pubk.curve = BR_EC_secp256r1;
		pubk.q = pubpt;
		pubk.qlen = br_ec_compute_pub(e, 0, pubpt, &privk);
		for (i = 0; i < 32; i++)
			hash[i] = (unsigned char)(i * 3);
		sl = br_ecdsa_i31_sign_raw(e, &br_sha256_vtable, hash, &privk, sig);
		if (sl != 64)
			say("  (signing failed)\r\n");
		ec = e;
		vrfy = br_ecdsa_i31_vrfy_raw;
		ecdsa = timeit(ecdsa_verify, 1);
		say("  i31/m31 %5lu ms\r\n", ecdsa);
		ec = &br_ec_p256_m15;
		vrfy = br_ecdsa_i15_vrfy_raw;
		{
			unsigned long t = timeit(ecdsa_verify, 1);
			say("  i15/m15 %5lu ms\r\n", t);
			if (t < ecdsa) ecdsa = t;
		}
	}

	/* RSA-2048 public */
	say("RSA-2048 signature check (public key, e=65537):\r\n");
	for (i = 0; i < 256; i++) {
		rsa_n[i] = (unsigned char)(0xA5 ^ (i * 37));
		rsa_x[i] = (unsigned char)(i * 11);
	}
	rsa_n[0] |= 0x80;
	rsa_n[255] |= 1;
	rsa_x[0] = 0x12;
	rsak.n = rsa_n;
	rsak.nlen = 256;
	rsak.e = rsa_e;
	rsak.elen = 3;
	rsapub = br_rsa_i31_public;
	rsa = timeit(rsa_verify, 1);
	say("  i31 %5lu ms\r\n", rsa);
	{
		unsigned long t;
		rsapub = br_rsa_i32_public;
		t = timeit(rsa_verify, 1);
		say("  i32 %5lu ms\r\n", t);
		if (t < rsa) rsa = t;
		rsapub = br_rsa_i15_public;
		t = timeit(rsa_verify, 1);
		say("  i15 %5lu ms\r\n", t);
		if (t < rsa) rsa = t;
	}

	/* bulk */
	say("Reading mail once connected (KB/s):\r\n");
	br_aes_ct_ctr_init(&aes_ct, key, 16);
	br_aes_big_ctr_init(&aes_big, key, 16);
	br_aes_small_ctr_init(&aes_small, key, 16);
	gh = br_ghash_ctmul32;
	say("  SHA-256            %5lu\r\n", kbs(timeit(sha256_4k, 2)));
	say("  AES-128 big        %5lu\r\n", kbs(timeit(aes_big_4k, 2)));
	say("  AES-128 small      %5lu\r\n", kbs(timeit(aes_small_4k, 2)));
	say("  GHASH ctmul32      %5lu\r\n", kbs(timeit(ghash_4k, 2)));
	gh = br_ghash_ctmul;
	say("  GHASH ctmul        %5lu\r\n", kbs(timeit(ghash_4k, 2)));
	say("  AES-128-GCM (ct)   %5lu\r\n", kbs(timeit(gcm_ct_4k, 2)));
	say("  AES-128-GCM (big)  %5lu\r\n", kbs(timeit(gcm_big_4k, 2)));
	say("  ChaCha20-Poly1305  %5lu\r\n", kbs(timeit(chapoly_4k, 2)));

	best_sig = ecdsa < rsa ? ecdsa : rsa;
	say("\r\nHandshake estimate (2 x X25519 + 3 signature checks):\r\n");
	say("  with RSA certificates:   %lu.%lu s\r\n", (2 * x25519 + 3 * rsa) / 1000, (2 * x25519 + 3 * rsa) % 1000 / 100);
	say("  with ECDSA certificates: %lu.%lu s\r\n", (2 * x25519 + 3 * ecdsa) / 1000, (2 * x25519 + 3 * ecdsa) % 1000 / 100);
	(void)best_sig;
	{
		long h = Fcreate("TLSBENCH.TXT", 0);
		if (h >= 0) {
			Fwrite((short)h, olen, out);
			Fclose((short)h);
			say("Saved in TLSBENCH.TXT.\r\n");
		}
	}
	say("Press a key.\r\n");
	Bconin(2);
	return 0;
}
