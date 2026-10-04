/*
 * arm/crc32_impl.h - AArch64 implementation of CRC-32/BZIP2
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

#ifndef LIB_ARM_CRC32_IMPL_H
#define LIB_ARM_CRC32_IMPL_H

#include "cpu_features.h"

/* The CRC kernel of the selected tier: the CRC32 instructions for NEON. */
static inline crc32_func_t
arch_select_crc32_func(void)
{
	if (arch_select_tier() == BZNG_TIER_NEON)
		return bzng_crc32_armv8;
	return NULL;
}
#define arch_select_crc32_func arch_select_crc32_func

#endif /* LIB_ARM_CRC32_IMPL_H */
