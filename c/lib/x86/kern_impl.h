/*
 * x86/kern_impl.h - x86 kernel tiers and their selection
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

#ifndef LIB_X86_KERN_IMPL_H
#define LIB_X86_KERN_IMPL_H

#include "cpu_features.h"

DECLARE_TIER_KERNELS(sse4)
DECLARE_TIER_KERNELS(avx2)

/*
 * The best tier the CPU supports: AVX2 (x86-64-v3) or SSE4 (x86-64-v2), each
 * with PCLMULQDQ for the CRC.
 */
static inline enum bzng_tier
arch_select_tier_x86(void)
{
	const u32 features = get_x86_cpu_features();

	if (HAVE_X86_V3(features) && HAVE_PCLMULQDQ(features))
		return BZNG_TIER_AVX2;
	if (HAVE_X86_V2(features) && HAVE_PCLMULQDQ(features))
		return BZNG_TIER_SSE4;
	return BZNG_TIER_SCALAR;
}
#define arch_select_tier arch_select_tier_x86

/* Defines arch_select_<name>(): the selected tier's kernel, NULL for scalar. */
#define ARCH_SELECT_KERNEL(name)                             \
	static inline name##_func_t arch_select_##name(void) \
	{                                                    \
		switch (arch_select_tier()) {                \
		case BZNG_TIER_AVX2:                         \
			return bzng_##name##_avx2;           \
		case BZNG_TIER_SSE4:                         \
			return bzng_##name##_sse4;           \
		default:                                     \
			return NULL;                         \
		}                                            \
	}

#endif /* LIB_X86_KERN_IMPL_H */
