/*
 * ref_state.h - what the reference libbz2's decoder state says
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

#ifndef FUZZ_REF_STATE_H
#define FUZZ_REF_STATE_H

#include "bzlib.h"

/*
 * Whether the stock libbz2 decoder behind strm (initialised by
 * ref_BZ2_bzDecompressInit and not yet ended) is idle: it has read its
 * stream's trailer, so its last call returned BZ_STREAM_END or reported a
 * combined-CRC mismatch.  Reads one field of the state; calls nothing.
 */
int ref_decompress_idle(const bz_stream *strm);

#endif /* FUZZ_REF_STATE_H */
