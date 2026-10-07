/*
 * memtest.c - peak memory per stream against the stated bounds
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * Peak bytes held through a counting bzalloc, at every level, against:
 *
 *   compression                        400,000 + 8 x block size
 *   decompression, in-formula layout   100,000 + 4 x block size
 *   decompression, wide layout         100,000 + 5 x block size
 *   decompression, small = 1           100,000 + 2.5 x block size
 *   BZ2_bzDecompressInit's default     the bound of the configured layout
 *
 * where block size = 100,000 x level.  The input is random bytes, so every
 * block is full.  Any peak over its bound fails the test.  Also checks that
 * a bzalloc returning memory aligned to only 4 bytes is enough.
 *
 * usage: memtest [-v]   (-v prints one line per level)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bzlib.h"
#include "dec.h"
#include "test_tier.h"

/* Counting allocator state. */
typedef struct {
	size_t in_use, peak;
	long calls;
	int misalign; /* return addresses that are 4 mod 8 */
} counter;

/*
 * bzalloc that records the live and peak bytes; with misalign set every
 * block it returns is 4-byte but not 8-byte aligned.
 */
static void *
count_alloc(void *opaque, int items, int size)
{
	counter *c = opaque;
	size_t n = (size_t)items * (size_t)size;
	uint8_t *p = malloc(n + 32);
	if (p == NULL)
		return NULL;
	memcpy(p, &n, sizeof n);
	c->calls++;
	c->in_use += n;
	if (c->in_use > c->peak)
		c->peak = c->in_use;
	return p + (c->misalign ? 20 : 16);
}

/* bzfree for count_alloc. */
static void
count_free(void *opaque, void *addr)
{
	counter *c = opaque;
	if (addr == NULL)
		return;
	uint8_t *p = (uint8_t *)addr - (c->misalign ? 20 : 16);
	size_t n;
	memcpy(&n, p, sizeof n);
	c->in_use -= n;
	free(p);
}

static int failures, verbose;

/* Records a failure. */
static void
fail(const char *what, int level, size_t peak, size_t budget)
{
	printf("FAIL %s level %d: peak %zu over %zu\n", what, level, peak,
	       budget);
	failures++;
}

/*
 * Compresses in[0..n) at level with a counting allocator; returns the
 * stream in *out (malloc'd) and its length, and the peak in *peak.
 */
static size_t
compress_counted(const uint8_t *in, size_t n, int level, int misalign,
		 uint8_t **out, size_t *peak)
{
	counter c = {0, 0, 0, misalign};
	bz_stream s;
	*peak = 0;
	memset(&s, 0, sizeof s);
	s.bzalloc = count_alloc;
	s.bzfree = count_free;
	s.opaque = &c;
	size_t cap = n + n / 50 + 4096;
	*out = malloc(cap);
	if (BZ2_bzCompressInit(&s, level, 0, 0) != BZ_OK) {
		printf("FAIL compress init level %d\n", level);
		failures++;
		return 0;
	}
	s.next_in = (char *)in;
	s.avail_in = (unsigned)n;
	s.next_out = (char *)*out;
	s.avail_out = (unsigned)cap;
	int rc;
	while ((rc = BZ2_bzCompress(&s, BZ_FINISH)) == BZ_FINISH_OK) {
	}
	if (rc != BZ_STREAM_END) {
		printf("FAIL compress level %d: rc %d\n", level, rc);
		failures++;
	}
	size_t len = cap - s.avail_out;
	BZ2_bzCompressEnd(&s);
	if (c.in_use != 0) {
		printf("FAIL compress level %d: %zu bytes not freed\n", level,
		       c.in_use);
		failures++;
	}
	*peak = c.peak;
	return len;
}

/*
 * Decompresses comp[0..len) with the given layout and small setting and a
 * counting allocator, checks the output against in[0..n), and returns the
 * peak.
 */
static size_t
decompress_counted(const uint8_t *comp, size_t len, const uint8_t *in, size_t n,
		   int layout, int small, int misalign)
{
	counter c = {0, 0, 0, misalign};
	bz_stream s;
	memset(&s, 0, sizeof s);
	s.bzalloc = count_alloc;
	s.bzfree = count_free;
	s.opaque = &c;
	uint8_t *out = malloc(n + 1);
	if ((layout < 0 ?
		     BZ2_bzDecompressInit(&s, 0, small) :
		     bzng_decompress_init(&s, 0, small, layout)) != BZ_OK) {
		printf("FAIL decompress init\n");
		failures++;
		return 0;
	}
	s.next_in = (char *)comp;
	s.avail_in = (unsigned)len;
	s.next_out = (char *)out;
	s.avail_out = (unsigned)n + 1;
	int rc = BZ2_bzDecompress(&s);
	if (rc != BZ_STREAM_END || s.avail_out != 1 ||
	    memcmp(out, in, n) != 0) {
		printf("FAIL decompress (layout %d, small %d): rc %d\n", layout,
		       small, rc);
		failures++;
	}
	BZ2_bzDecompressEnd(&s);
	if (c.in_use != 0) {
		printf("FAIL decompress: %zu bytes not freed\n", c.in_use);
		failures++;
	}
	free(out);
	return c.peak;
}

int
main(int argc, char **argv)
{
	verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
	require_requested_tier();
	uint64_t x = 0x9E3779B97F4A7C15ull;
	for (int level = 1; level <= 9; level++) {
		const size_t block = 100000 * (size_t)level;
		const size_t n = 2 * block + 12345;
		uint8_t *in = malloc(n);
		for (size_t i = 0; i < n; i++) {
			x ^= x << 13;
			x ^= x >> 7;
			x ^= x << 17;
			in[i] = (uint8_t)x;
		}
		for (int misalign = 0; misalign <= 1; misalign++) {
			uint8_t *comp;
			size_t enc_peak = 0;
			size_t len = compress_counted(in, n, level, misalign,
						      &comp, &enc_peak);
			size_t fit = decompress_counted(
				comp, len, in, n, BZNG_LAYOUT_FIT, 0, misalign);
			size_t wide = decompress_counted(comp, len, in, n,
							 BZNG_LAYOUT_WIDE, 0,
							 misalign);
			size_t small = decompress_counted(
				comp, len, in, n, BZNG_LAYOUT_FIT, 1, misalign);
			size_t dflt = decompress_counted(comp, len, in, n, -1,
							 0, misalign);
			const size_t enc_budget = 400000 + 8 * block,
				     fit_budget = 100000 + 4 * block;
			const size_t wide_budget = 100000 + 5 * block,
				     small_budget = 100000 + 5 * block / 2;
			const size_t dflt_budget =
				BZNG_DEFAULT_LAYOUT == BZNG_LAYOUT_FIT ?
					fit_budget :
					wide_budget;
			if (enc_peak > enc_budget)
				fail("compress", level, enc_peak, enc_budget);
			if (fit > fit_budget)
				fail("decompress (in-formula layout)", level,
				     fit, fit_budget);
			if (wide > wide_budget)
				fail("decompress (wide layout)", level, wide,
				     wide_budget);
			if (small > small_budget)
				fail("decompress (small)", level, small,
				     small_budget);
			if (dflt > dflt_budget)
				fail("decompress (default layout)", level, dflt,
				     dflt_budget);
			if (verbose && !misalign)
				printf("level\t%d\tcompress\t%zu\t%zu\tfit\t%"
				       "zu\t%zu\twide\t%zu\t%zu\tsmall\t%zu\t%"
				       "zu\tdefault\t%zu\t%zu\n",
				       level, enc_peak, enc_budget, fit,
				       fit_budget, wide, wide_budget, small,
				       small_budget, dflt, dflt_budget);
			free(comp);
		}
		free(in);
	}
	printf("memtest: %s\n",
	       failures ? "FAILED" : "all peaks inside the formulas");
	return failures ? 1 : 0;
}
