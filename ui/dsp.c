/*
 * dsp.c - Falcon mode's arithmetic on the DSP56001: the RSA public-key
 * operation that checks a server's signatures. Without a DSP (or with
 * Falcon mode off) BearSSL does it on the 68030.
 */
#include <string.h>
#include "ui.h"
#include "../src/tls.h"

static int have_dsp;

void falcon_dsp_init(void)
{
	tls_rsa_accel = 0;
	have_dsp = 0;
}

const char *falcon_dsp_state(void)
{
	return have_dsp ? "DSP" : "68030";
}
