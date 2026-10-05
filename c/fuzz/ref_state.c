/*
 * ref_state.c - what the reference libbz2's decoder state says
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * libbz2's own declarations of its state, from test/reference (the quoted
 * include of bzlib.h there finds the reference's copy beside it), in a
 * translation unit of their own: bz2-ng's lib/bzlib_private.h, which the
 * targets reach through lib/dec.h, has the same include guard and no DState.
 */
#include "reference/bzlib_private.h"

#include "ref_state.h"

/*
 * libbz2 sets BZ_X_IDLE just before it compares the combined CRC
 * (test/reference/decompress.c:611); every other error leaves the state at
 * the step where it was raised.
 */
int
ref_decompress_idle(const bz_stream *strm)
{
	const DState *s = strm->state;

	return s->state == BZ_X_IDLE;
}
