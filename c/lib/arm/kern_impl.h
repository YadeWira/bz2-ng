/*
 * arm/kern_impl.h - AArch64 kernel tier and its selection
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

#ifndef LIB_ARM_KERN_IMPL_H
#define LIB_ARM_KERN_IMPL_H

#include "cpu_features.h"

DECLARE_TIER_KERNELS(neon)

/* The best tier the CPU supports: NEON with the CRC32 instructions. */
static inline enum bzng_tier
arch_select_tier_arm(void)
{
	const u32 features = get_arm_cpu_features();

	if (HAVE_NEON(features) && HAVE_CRC32(features))
		return BZNG_TIER_NEON;
	return BZNG_TIER_SCALAR;
}
#define arch_select_tier arch_select_tier_arm

/* Defines arch_select_<name>(): the selected tier's kernel, NULL for scalar. */
#define ARCH_SELECT_KERNEL(name)                             \
	static inline name##_func_t arch_select_##name(void) \
	{                                                    \
		if (arch_select_tier() == BZNG_TIER_NEON)    \
			return bzng_##name##_neon;           \
		return NULL;                                 \
	}

#endif /* LIB_ARM_KERN_IMPL_H */
