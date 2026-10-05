/*
 * huff_limit_test.c - Huffman codes that need libbz2's 17-bit limit
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * libbz2 limits Huffman codes to 17 bits: when a coding table's tree is
 * deeper, it halves every weight and builds the tree again, as often as it
 * takes (hb_make_code_lengths in lib/enc_huff.c).  Ordinary data never gets
 * there, because a code that long needs a table whose symbol counts grow
 * steeply over 19 or more symbols.  This test builds such an input,
 * compresses it with stock libbz2 and with bz2-ng, requires the same bytes,
 * and decodes bz2-ng's stream.
 *
 * The Huffman stage codes the MTF ranks of the block's BWT.  Here rank r
 * (1 to RANKS) occurs 2^(RANKS - r) times, so each count exceeds the sum of
 * all the smaller ones, the tree is a chain, and the rarest rank's code is
 * about RANKS bits long.  The test chooses that rank sequence, turns it into
 * a BWT by inverse MTF and the BWT into a text by inverting it.  The ranks
 * are shuffled so that every 50-symbol group sees the same mixture and most
 * groups keep to one coding table; unshuffled, the text would be so
 * repetitive that the run-length stage would change it.  What still differs
 * from the plan (runs of four that the run-length stage rewrites, rotations
 * that cross from one cycle's word into the next) adds a few stray counts,
 * which a doubling chain absorbs.  Measured with an instrumented libbz2: the
 * deepest code before limiting is 20 bits, and four of the block's 24
 * code-length computations (six tables, four iterations) are limited, with
 * seven halving passes, two of them in one computation.
 *
 * The text is far from periodic, so the comparison is exact: the origPtr
 * exception of docs/spec.md section 2.3 needs an exactly periodic block.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bzlib.h"
#include "reference_api.h"
#include "test_tier.h"

/* MTF ranks 1..RANKS; the text is 2^RANKS - 1 bytes, one block at level 9. */
#define RANKS 19
#define LEVEL 9

static uint64_t rng_state = 1;

/* splitmix64 */
static uint64_t
rnd(void)
{
	uint64_t z = (rng_state += 0x9e3779b97f4a7c15ULL);

	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

/* malloc that exits on failure. */
static void *
xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);

	if (p == NULL) {
		printf("out of memory (%zu bytes)\n", n);
		exit(2);
	}
	return p;
}

/*
 * Writes the n = 2^RANKS - 1 bytes of the test's text to text[] (see the
 * comment at the top of the file).
 */
static void
make_text(uint8_t *text, size_t n)
{
	uint8_t *rank = xmalloc(n), *bwt = xmalloc(n);
	int32_t *lf = xmalloc(n * sizeof(*lf));
	size_t count[256] = {0}, next[256];
	uint8_t mtf[RANKS + 1];

	/*
	 * 1 + the number of trailing zeros of i: rank r occurs 2^(RANKS - r)
	 * times among i = 1..n.
	 */
	for (size_t i = 0; i < n; i++) {
		size_t v = i + 1;
		uint8_t r = 1;

		while ((v & 1) == 0) {
			v >>= 1;
			r++;
		}
		rank[i] = r;
	}
	for (size_t i = n - 1; i > 0; i--) {
		size_t j = (size_t)(rnd() % (i + 1));
		uint8_t t = rank[i];

		rank[i] = rank[j];
		rank[j] = t;
	}

	/* Inverse MTF from libbz2's initial list, the bytes in use in order. */
	for (int k = 0; k <= RANKS; k++)
		mtf[k] = (uint8_t)k;
	for (size_t i = 0; i < n; i++) {
		uint8_t c = mtf[rank[i]];

		memmove(mtf + 1, mtf, rank[i]);
		mtf[0] = c;
		bwt[i] = c;
	}

	/* LF mapping: the row of the rotation that starts one byte earlier. */
	for (size_t i = 0; i < n; i++)
		count[bwt[i]]++;
	for (size_t c = 0, sum = 0; c < 256; c++) {
		next[c] = sum;
		sum += count[c];
	}
	for (size_t i = 0; i < n; i++)
		lf[i] = (int32_t)next[bwt[i]]++;

	/*
	 * Each cycle of the LF mapping is the BWT of one word; the text is the
	 * words one after another, each read backwards from its cycle.  A
	 * visited row's LF entry becomes -1 - entry.
	 */
	size_t pos = n;
	for (size_t start = 0; start < n; start++) {
		if (lf[start] < 0)
			continue;
		int32_t row = (int32_t)start;
		do {
			int32_t prev = lf[row];

			text[--pos] = bwt[row];
			lf[row] = -1 - prev;
			row = prev;
		} while (row != (int32_t)start);
	}
	free(rank);
	free(bwt);
	free(lf);
}

int
main(void)
{
	require_requested_tier();
	const size_t n = ((size_t)1 << RANKS) - 1;
	const unsigned cap = (unsigned)(n + n / 50 + 4096);
	uint8_t *text = xmalloc(n), *out = xmalloc(n + 1);
	char *ref = xmalloc(cap), *ng = xmalloc(cap);
	unsigned ref_len = cap, ng_len = cap, out_len = (unsigned)n + 1;
	int failures = 0;

	make_text(text, n);
	int ref_rc = ref_BZ2_bzBuffToBuffCompress(ref, &ref_len, (char *)text,
						  (unsigned)n, LEVEL, 0, 0);
	int ng_rc = BZ2_bzBuffToBuffCompress(ng, &ng_len, (char *)text,
					     (unsigned)n, LEVEL, 0, 0);
	if (ref_rc != BZ_OK || ng_rc != BZ_OK) {
		printf("FAIL compress rc: libbz2 %d, bz2-ng %d\n", ref_rc,
		       ng_rc);
		return 1;
	}
	if (ref_len != ng_len || memcmp(ref, ng, ref_len) != 0) {
		size_t d = 0;

		while (d < ref_len && d < ng_len && ref[d] == ng[d])
			d++;
		printf("FAIL streams differ at byte %zu (libbz2 %u bytes, "
		       "bz2-ng %u)\n",
		       d, ref_len, ng_len);
		failures++;
	}
	int rc = BZ2_bzBuffToBuffDecompress((char *)out, &out_len, ng, ng_len,
					    0, 0);
	if (rc != BZ_OK || out_len != n || memcmp(out, text, n) != 0) {
		printf("FAIL bz2-ng does not decode its stream (rc %d)\n", rc);
		failures++;
	}
	printf("huff_limit: %zu bytes, level %d, %u compressed, tier %s: %s\n",
	       n, LEVEL, ng_len, bzng_tier_name(),
	       failures ? "FAILED" : "identical to libbz2");
	free(text);
	free(out);
	free(ref);
	free(ng);
	return failures ? 1 : 0;
}
