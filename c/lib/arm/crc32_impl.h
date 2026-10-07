/*
 * arm/crc32_impl.h - AArch64 implementation of CRC-32/BZIP2
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

#ifndef LIB_ARM_CRC32_IMPL_H
#define LIB_ARM_CRC32_IMPL_H

#include "cpu_features.h"

/*
 * The CRC kernel of the selected tier: for NEON, carry-less folding where
 * the CPU has PMULL, the CRC32 instructions alone otherwise.
 */
static inline crc32_func_t
arch_select_crc32_func(void)
{
	if (arch_select_tier() != BZNG_TIER_NEON)
		return NULL;
	if (HAVE_PMULL(get_arm_cpu_features()))
		return bzng_crc32_pmull;
	return bzng_crc32_armv8;
}
#define arch_select_crc32_func arch_select_crc32_func

#endif /* LIB_ARM_CRC32_IMPL_H */
