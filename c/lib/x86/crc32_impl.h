/*
 * x86/crc32_impl.h - x86 implementations of CRC-32/BZIP2
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

#ifndef LIB_X86_CRC32_IMPL_H
#define LIB_X86_CRC32_IMPL_H

#include "cpu_features.h"

/*
 * The CRC kernel of the selected tier: carry-less multiplication on 256-bit
 * vectors for the AVX2 tier on CPUs with VPCLMULQDQ, on 128-bit vectors for
 * the SSE4 and AVX2 tiers otherwise.
 */
static inline crc32_func_t
arch_select_crc32_func(void)
{
	const enum bzng_tier tier = arch_select_tier();

	if (tier == BZNG_TIER_AVX2 && HAVE_VPCLMULQDQ(get_x86_cpu_features()))
		return bzng_crc32_vpclmul;
	if (tier == BZNG_TIER_AVX2 || tier == BZNG_TIER_SSE4)
		return bzng_crc32_pclmul;
	return NULL;
}
#define arch_select_crc32_func arch_select_crc32_func

#endif /* LIB_X86_CRC32_IMPL_H */
