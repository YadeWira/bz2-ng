/*
 * arm/cpu_features.h - feature detection for ARM CPUs
 *
 * Copyright 2016 Eric Biggers
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Adapted from libdeflate 1.26 (lib/arm/cpu_features.h) for bz2-ng: AArch64
 * only, and only the features bz2-ng's NEON tier needs (NEON, the CRC32
 * instructions, and PMULL for its faster CRC kernel).
 */

#ifndef LIB_ARM_CPU_FEATURES_H
#define LIB_ARM_CPU_FEATURES_H

#include "../lib_common.h"

#if defined(ARCH_ARM64)

#define ARM_CPU_FEATURE_NEON		(1 << 0)
#define ARM_CPU_FEATURE_CRC32		(1 << 1)
#define ARM_CPU_FEATURE_PMULL		(1 << 2)

#define HAVE_NEON(features)	((features) & ARM_CPU_FEATURE_NEON)
#define HAVE_CRC32(features)	((features) & ARM_CPU_FEATURE_CRC32)
#define HAVE_PMULL(features)	((features) & ARM_CPU_FEATURE_PMULL)

#if defined(__linux__) || defined(__APPLE__)
/* Runtime ARM CPU feature detection is supported. */
#  define ARM_CPU_FEATURES_KNOWN	(1U << 31)
extern volatile u32 bzng_arm_cpu_features;

void bzng_init_arm_cpu_features(void);

/*
 * The features of the running CPU, restricted by $BZ2NG_TIER.  Computed on
 * first use; threads that race on it compute and store the same value.
 */
static inline u32 get_arm_cpu_features(void)
{
	if (bzng_arm_cpu_features == 0)
		bzng_init_arm_cpu_features();
	return bzng_arm_cpu_features;
}
#else
static inline u32 get_arm_cpu_features(void) { return 0; }
#endif

#endif /* ARCH_ARM64 */

#endif /* LIB_ARM_CPU_FEATURES_H */
