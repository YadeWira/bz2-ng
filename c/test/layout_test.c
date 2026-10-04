/*
 * layout_test.c - compile-time check of the public ABI
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * The layout of bz_stream and the constants of bzlib.h are libbz2 1.0.8's.
 * The header is also compared byte for byte with test/reference/bzlib.h by
 * CTest; this checks what a compiler sees, so that a header edit that keeps
 * the bytes plausible but moves a field cannot pass.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "bzlib.h"

#define P sizeof(void *)

_Static_assert(offsetof(bz_stream, next_in) == 0, "next_in");
_Static_assert(offsetof(bz_stream, avail_in) == P, "avail_in");
_Static_assert(offsetof(bz_stream, total_in_lo32) == P + 4, "total_in_lo32");
_Static_assert(offsetof(bz_stream, total_in_hi32) == P + 8, "total_in_hi32");
_Static_assert(offsetof(bz_stream, next_out) == (P == 8 ? 24 : 16), "next_out");
_Static_assert(offsetof(bz_stream, avail_out) == (P == 8 ? 32 : 20),
	       "avail_out");
_Static_assert(offsetof(bz_stream, total_out_lo32) == (P == 8 ? 36 : 24),
	       "total_out_lo32");
_Static_assert(offsetof(bz_stream, total_out_hi32) == (P == 8 ? 40 : 28),
	       "total_out_hi32");
_Static_assert(offsetof(bz_stream, state) == (P == 8 ? 48 : 32), "state");
_Static_assert(offsetof(bz_stream, bzalloc) == (P == 8 ? 56 : 36), "bzalloc");
_Static_assert(offsetof(bz_stream, bzfree) == (P == 8 ? 64 : 40), "bzfree");
_Static_assert(offsetof(bz_stream, opaque) == (P == 8 ? 72 : 44), "opaque");
_Static_assert(sizeof(bz_stream) == (P == 8 ? 80 : 48), "sizeof bz_stream");

_Static_assert(BZ_RUN == 0 && BZ_FLUSH == 1 && BZ_FINISH == 2, "actions");
_Static_assert(BZ_OK == 0 && BZ_RUN_OK == 1 && BZ_FLUSH_OK == 2 &&
		       BZ_FINISH_OK == 3 && BZ_STREAM_END == 4,
	       "return codes");
_Static_assert(BZ_SEQUENCE_ERROR == -1 && BZ_PARAM_ERROR == -2 &&
		       BZ_MEM_ERROR == -3 && BZ_DATA_ERROR == -4 &&
		       BZ_DATA_ERROR_MAGIC == -5 && BZ_IO_ERROR == -6 &&
		       BZ_UNEXPECTED_EOF == -7 && BZ_OUTBUFF_FULL == -8 &&
		       BZ_CONFIG_ERROR == -9,
	       "error codes");
_Static_assert(BZ_MAX_UNUSED == 5000, "BZ_MAX_UNUSED");

int
main(void)
{
	const char *v = BZ2_bzlibVersion();

	if (strncmp(v, "1.0.8, ", 7) != 0) {
		printf("FAIL: BZ2_bzlibVersion() is \"%s\"\n", v);
		return 1;
	}
	printf("layout ok; version \"%s\"\n", v);
	return 0;
}
