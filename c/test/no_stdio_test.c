/*
 * no_stdio_test.c - the library built with BZ_NO_STDIO
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * A BZ_NO_STDIO build has no FILE* layer and calls the integrator's
 * bz_internal_error on an internal error.  This supplies one and checks the
 * buffer-to-buffer functions: a round trip, an output buffer that is too
 * small, and corrupt input.  no_stdio_symbols.cmake checks that the library
 * references no stdio function.
 */

#ifndef BZ_NO_STDIO
#  error "this test is built only with BZ_NO_STDIO (BZ2NG_NO_STDIO=ON)"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bzlib.h"

/* Reports a failed check; returns the test's exit status. */
static int
failed(const char *what, int rc)
{
	fprintf(stderr, "no_stdio_test: %s (return code %d)\n", what, rc);
	return 1;
}

/* The integrator's internal-error handler, required by BZ_NO_STDIO. */
void
bz_internal_error(int errcode)
{
	(void)errcode;
	abort();
}

int
main(void)
{
	static char in[300000], comp[310000], out[300001];
	unsigned clen = sizeof(comp), dlen = sizeof(out), small = 10;
	int rc;

	for (size_t i = 0; i < sizeof(in); i++)
		in[i] = "abcabd"[(i * 7 + i / 13) % 6];
	rc = BZ2_bzBuffToBuffCompress(comp, &clen, in, sizeof(in), 9, 0, 0);
	if (rc != BZ_OK)
		return failed("compression", rc);
	rc = BZ2_bzBuffToBuffDecompress(out, &dlen, comp, clen, 0, 0);
	if (rc != BZ_OK || dlen != sizeof(in) || memcmp(in, out, dlen) != 0)
		return failed("round trip", rc);
	rc = BZ2_bzBuffToBuffCompress(out, &small, in, sizeof(in), 9, 0, 0);
	if (rc != BZ_OUTBUFF_FULL)
		return failed("10-byte output buffer is not BZ_OUTBUFF_FULL",
			      rc);
	comp[clen / 2] ^= 0x10;
	dlen = sizeof(out);
	rc = BZ2_bzBuffToBuffDecompress(out, &dlen, comp, clen, 0, 0);
	if (rc != BZ_DATA_ERROR)
		return failed("corrupt input is not BZ_DATA_ERROR", rc);
	printf("no_stdio_test: ok\n");
	return 0;
}
