/*
 * fuzz_sort.c - libFuzzer target: the encoder's sort stage against an oracle
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * The input is one byte that chooses how the block is built, followed by the
 * block's bytes.  The block goes through the encoder's sort stage
 * (bzng_sort_block: least rotation, then bzng_bwt on it or on its root) and
 * the BWT, origPtr and byte frequencies are compared with the independent
 * oracle of test/sort_oracle.h (cyclic prefix doubling, cross-checked by a
 * naive rotation sort up to 64 bytes), as test/sort_test.c does: the BWT
 * must match exactly, and origPtr must be the last row of rotation 0's group
 * of equal rotations (its only row when the block is primitive).
 *
 * Byte 0: bit 7 clear, the block is the rest of the input as it is; bit 7
 * set, it is the rest repeated 2 + (bits 0-3) times and then the first
 * (bits 4-6) eighths of it once more, so that small inputs give long exactly
 * periodic and nearly periodic blocks.  Blocks are cut to SORT_MAX_N bytes,
 * the encoder's largest.
 *
 * The sorter, its work space and the block are exactly as large as
 * bzng_sort_block requires, so that AddressSanitizer sees any access past
 * them.
 */

#include "enc.h"
#include "fuzz_common.h"
#include "sort.h"
#include "sort_oracle.h"

/* The encoder's largest block: level 9, 900,000 - 19 bytes after RLE1. */
#define SORT_MAX_N 899981

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* libFuzzer's set-up hook: checks the kernel tier, allocates the oracle. */
int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc;
	(void)argv;
	fuzz_init_tier("fuzz_sort");
	oracle_alloc(SORT_MAX_N);
	return 0;
}

/* Sorts one block with the sort stage and the oracle (see the file comment). */
int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < 2)
		return 0;
	const uint8_t mode = data[0];
	const uint8_t *body = data + 1;
	const size_t body_len = size - 1;
	size_t n = body_len;
	if (mode & 0x80)
		n = body_len * (2 + (mode & 15)) +
		    body_len * ((mode >> 4) & 7) / 8;
	if (n > SORT_MAX_N)
		n = SORT_MAX_N;

	bzng_enc_core core;
	memset(&core, 0, sizeof core);
	core.cap = (s32)n;
	core.tt = malloc(2 * n + BZNG_ENC_PAD);
	for (size_t i = 0; i < n; i++)
		core.tt[i] = body[i % body_len];
	/* the sort stage overwrites tt with the BWT; the oracle keeps a copy */
	uint8_t *block = malloc(n);
	memcpy(block, core.tt, n);

	/* The oracle: sorted rotations, ranks, and the expected outputs. */
	oracle_sort(block, (int32_t)n);
	if (n <= 64) {
		int32_t idx[64];
		for (int32_t i = 0; i < (int32_t)n; i++)
			idx[i] = i;
		naive_T = block;
		naive_n = (int32_t)n;
		qsort(idx, n, sizeof idx[0], naive_cmp);
		for (int32_t i = 0; i < (int32_t)n; i++)
			if (o_rank[idx[i]] != o_rank[o_sa[i]])
				fuzz_fail("sort n=%zu: the oracles disagree at "
					  "row %d",
					  n, i);
	}
	uint8_t *expect_L = malloc(n);
	int32_t last = 0;
	for (int32_t i = 0; i < (int32_t)n; i++) {
		const int32_t j = o_sa[i];
		expect_L[i] = block[j == 0 ? n - 1 : (size_t)j - 1];
		if (o_rank[j] == o_rank[0])
			last = i;
	}

	/*
	 * Exactly the work bzng_bwt requires, as a full encoder block gets, and
	 * a non-zero fill, so that nothing relies on zeroed memory.
	 */
	core.work_ints = bzng_bwt_work_ints((s32)n);
	core.work = malloc(core.work_ints * sizeof(int32_t));
	memset(core.work, (n & 1) ? 0xA5 : 0xFF,
	       core.work_ints * sizeof(int32_t));
	int32_t freq[256];
	uint8_t *L;
	const int32_t orig = bzng_sort_block(&core, (s32)n, freq, &L);
	if (orig < 0)
		fuzz_fail("sort n=%zu: the sorter failed (%d)", n, orig);
	const size_t d = fuzz_first_difference(L, expect_L, 0, n);
	if (d < n)
		fuzz_fail("sort n=%zu: BWT differs at row %zu", n, d);
	if (orig != last)
		fuzz_fail("sort n=%zu: origPtr %d, expected %d", n, orig, last);
	int32_t count[256] = {0};
	for (size_t i = 0; i < n; i++)
		count[block[i]]++;
	if (memcmp(count, freq, sizeof count) != 0)
		fuzz_fail("sort n=%zu: byte frequencies differ", n);

	free(core.work);
	free(core.tt);
	free(block);
	free(expect_L);
	return 0;
}
