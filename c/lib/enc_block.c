/*
 * enc_block.c - the block encoder's core
 *
 * Block core: one RLE1 block in, its compressed bits appended to a bit
 * writer. Works on a completed block only, so any driver can frame streams
 * around it.
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */
#include "enc.h"

size_t
bzng_block_bound(s32 cap)
{
	size_t n = (size_t)cap + 1; /* MTF values incl. EOB */
	size_t data = (n * BZNG_MAX_CODE_LEN + 7) / 8; /* coded MTF values */
	size_t sel = ((n + BZ_G_SIZE - 1) / BZ_G_SIZE * 6 + 7) /
		     8; /* <= 6 bits each */
	/*
	 * Code lengths: 5 bits per table, then per symbol 1 bit plus 2 bits per
	 * unit of change, each change at most BZNG_MAX_CODE_LEN.
	 */
	size_t lens =
		(size_t)BZ_N_GROUPS *
			(5 + BZ_MAX_ALPHA_SIZE * (1 + 2 * BZNG_MAX_CODE_LEN)) /
			8 +
		1;
	/*
	 * carry 4, stream header 4, block header 14, symbol map 34, table
	 * counts 3, trailer 10, padding 1, then the bit writer's slack and the
	 * fast emitter's guard (enc.h).
	 */
	return data + sel + lens + 4 + 4 + 14 + 34 + 3 + 10 + 1 +
	       BZNG_BW_SLACK + BZNG_GROUP_GUARD;
}

size_t
bzng_zbits_offset(s32 cap)
{
	/*
	 * MTF values: at most cap + 1, plus 8 of overrun from the zero-run
	 * writer; rounded up to 16 bytes.
	 */
	return (((size_t)cap + 1 + 8) * sizeof(u16) + 15) & ~(size_t)15;
}

/* The MTF stage's kernel, chosen on first use (kern.h). */
static s32 dispatch_mtf(const u8 *L, s32 n, const u8 *in_use, u16 *mtfv,
			s32 *mtf_freq);

static volatile mtf_func_t mtf_impl = dispatch_mtf;

/* Choose the best implementation at runtime. */
static s32
dispatch_mtf(const u8 *L, s32 n, const u8 *in_use, u16 *mtfv, s32 *mtf_freq)
{
	mtf_func_t f = arch_select_mtf();

	if (f == NULL)
		f = bzng_mtf_scalar;

	mtf_impl = f;
	return f(L, n, in_use, mtfv, mtf_freq);
}

/*
 * Symbol map: 16 bits for which 16-byte ranges are used, then 16 bits per
 * used range.
 */
static void
put_symbol_map(bzng_bitwriter *w, const u8 *in_use)
{
	u32 used16 = 0;
	for (int i = 0; i < 16; i++)
		for (int j = 0; j < 16; j++)
			if (in_use[i * 16 + j])
				used16 |= 0x8000u >> i;
	bzng_bw_put(w, 16, used16);
	for (int i = 0; i < 16; i++) {
		if (!(used16 & (0x8000u >> i)))
			continue;
		u32 bits = 0;
		for (int j = 0; j < 16; j++)
			if (in_use[i * 16 + j])
				bits |= 0x8000u >> j;
		bzng_bw_put(w, 16, bits);
	}
}

u32
bzng_block_encode(bzng_enc_core *core, s32 nblock, u32 block_crc,
		  bzng_bitwriter *w)
{
	s32 freq[256];
	u8 *L;
	ASSERT(nblock >= 1 && nblock <= core->cap);
	s32 orig = bzng_sort_block(core, nblock, freq, &L);
	if (orig < 0)
		return 0;
	ASSERT(orig < nblock);

	u8 in_use[256];
	s32 n_in_use = 0;
	for (int c = 0; c < 256; c++) {
		in_use[c] = freq[c] != 0;
		n_in_use += in_use[c];
	}
	u16 *mtfv = (u16 *)(void *)core->work;
	s32 mtf_freq[BZ_MAX_ALPHA_SIZE];
	s32 nmtf = mtf_impl(L, nblock, in_use, mtfv, mtf_freq);

	bzng_bw_put(w, 24, 0x314159);
	bzng_bw_put(w, 24, 0x265359);
	bzng_bw_put(w, 32, block_crc);
	bzng_bw_put(w, 1, 0);
	bzng_bw_put(w, 24, (u32)orig);
	put_symbol_map(w, in_use);
	return bzng_huff_encode(core->huff, mtfv, nmtf, mtf_freq, n_in_use + 2,
				w);
}
