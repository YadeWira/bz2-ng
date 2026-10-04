/*
 * enc_sort.c - least rotation into the block sorter
 *
 * Sorter bridge: the block's least rotation into the block sorter.
 *
 * The least rotation T' of a primitive block is a Lyndon word, whose suffix
 * order equals its rotation order, so the suffix sorter of sort.c on T'
 * yields bzip2's rotation order. For T = u^m (m > 1), T' = u'^m with u'
 * Lyndon; its rotation order is the suffix order of u' with each row
 * repeated m times, and any of the m tied rows is a valid origPtr (here: the
 * last one). The sorter writes the BWT over the rotated block in place and
 * returns the row of the rotation that is the original block, which is
 * origPtr.
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */
#include "enc.h"
#include "sort.h"

/* The least rotation's kernel, chosen on first use (kern.h). */
static s32 dispatch_least_rotation(const u8 *tt, s32 n, s32 *cand,
				   int *periodic);

static volatile least_rotation_func_t least_rotation_impl =
	dispatch_least_rotation;

/* Choose the best implementation at runtime. */
static s32
dispatch_least_rotation(const u8 *tt, s32 n, s32 *cand, int *periodic)
{
	least_rotation_func_t f = arch_select_least_rotation();

	if (f == NULL)
		f = bzng_least_rotation_scalar;

	least_rotation_impl = f;
	return f(tt, n, cand, periodic);
}

/*
 * Length of the primitive root of a least rotation Tr (one Duval scan);
 * -1 if Tr is not a power of a Lyndon word.
 */
static s32
lyndon_root_len(const u8 *Tr, s32 n)
{
	s32 j = 1, k = 0;
	while (j < n) {
		if (Tr[k] < Tr[j])
			k = 0;
		else if (Tr[k] == Tr[j])
			k++;
		else
			return -1;
		j++;
	}
	s32 p = n - k;
	return (n % p == 0) ? p : -1;
}

/* bwt[0..len) -> each byte repeated m times, in place (back to front). */
static void
expand_bwt(u8 *bwt, s32 len, s32 m)
{
	for (s32 i = len - 1; i >= 0; i--)
		memset(bwt + (size_t)i * m, bwt[i], (size_t)m);
}

s32
bzng_sort_block(bzng_enc_core *core, s32 n, s32 *freq, u8 **L_out)
{
	u8 *tt = core->tt;

	memcpy(tt + n, tt, (size_t)n);
	memset(tt + 2 * (size_t)n, 0, BZNG_ENC_PAD);

	int periodic;
	s32 r = least_rotation_impl(tt, n, core->work, &periodic);
	s32 p = n;
	if (periodic) {
		p = lyndon_root_len(tt + r, n);
		if (p < 0 || p == n)
			return -1;
	}
	s32 m = n / p;
	s32 q = (n - r) % n;
	u8 *L = tt + r;
	s32 rank = bzng_bwt(tt + r, L, p, q % p, freq, core->work,
			    core->work_ints);
	if (rank < 0)
		return -1;
	if (m > 1) {
		expand_bwt(L, p, m);
		for (int c = 0; c < 256; c++)
			freq[c] *= m;
	}
	*L_out = L;
	return rank * m + m - 1;
}
