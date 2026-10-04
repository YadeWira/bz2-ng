/*
 * x86/cpu_features.h - feature detection for x86 CPUs
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
 * Adapted from libdeflate 1.26 (lib/x86/cpu_features.h) for bz2-ng: the
 * feature set is the one bz2-ng's tiers need (x86-64-v2, x86-64-v3,
 * PCLMULQDQ, VPCLMULQDQ), and every feature is detected at run time even if
 * the compiler targets it, because the tiers are separate translation units
 * with their own flags.
 */

#ifndef LIB_X86_CPU_FEATURES_H
#define LIB_X86_CPU_FEATURES_H

#include "../lib_common.h"

#if defined(ARCH_X86_32) || defined(ARCH_X86_64)

#define X86_CPU_FEATURE_SSE2		(1 << 0)
#define X86_CPU_FEATURE_SSSE3		(1 << 1)
#define X86_CPU_FEATURE_SSE4_1		(1 << 2)
#define X86_CPU_FEATURE_SSE4_2		(1 << 3)
#define X86_CPU_FEATURE_POPCNT		(1 << 4)
#define X86_CPU_FEATURE_CX16		(1 << 5)
#define X86_CPU_FEATURE_LAHF		(1 << 6)
#define X86_CPU_FEATURE_PCLMULQDQ	(1 << 7)
#define X86_CPU_FEATURE_AVX		(1 << 8)
#define X86_CPU_FEATURE_AVX2		(1 << 9)
#define X86_CPU_FEATURE_BMI1		(1 << 10)
#define X86_CPU_FEATURE_BMI2		(1 << 11)
#define X86_CPU_FEATURE_FMA		(1 << 12)
#define X86_CPU_FEATURE_F16C		(1 << 13)
#define X86_CPU_FEATURE_LZCNT		(1 << 14)
#define X86_CPU_FEATURE_MOVBE		(1 << 15)
#define X86_CPU_FEATURE_VPCLMULQDQ	(1 << 16)

/* The x86-64 microarchitecture levels the tiers are compiled for. */
#define X86_CPU_FEATURES_V2	(X86_CPU_FEATURE_SSE2 | X86_CPU_FEATURE_SSSE3 | \
				 X86_CPU_FEATURE_SSE4_1 | X86_CPU_FEATURE_SSE4_2 | \
				 X86_CPU_FEATURE_POPCNT | X86_CPU_FEATURE_CX16 | \
				 X86_CPU_FEATURE_LAHF)
#define X86_CPU_FEATURES_V3	(X86_CPU_FEATURES_V2 | X86_CPU_FEATURE_AVX | \
				 X86_CPU_FEATURE_AVX2 | X86_CPU_FEATURE_BMI1 | \
				 X86_CPU_FEATURE_BMI2 | X86_CPU_FEATURE_FMA | \
				 X86_CPU_FEATURE_F16C | X86_CPU_FEATURE_LZCNT | \
				 X86_CPU_FEATURE_MOVBE)

#define HAVE_X86_V2(features) \
	(((features) & X86_CPU_FEATURES_V2) == X86_CPU_FEATURES_V2)
#define HAVE_X86_V3(features) \
	(((features) & X86_CPU_FEATURES_V3) == X86_CPU_FEATURES_V3)
#define HAVE_PCLMULQDQ(features)	((features) & X86_CPU_FEATURE_PCLMULQDQ)
#define HAVE_VPCLMULQDQ(features)	((features) & X86_CPU_FEATURE_VPCLMULQDQ)

#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
/* Runtime x86 CPU feature detection is supported. */
#  define X86_CPU_FEATURES_KNOWN	(1U << 31)
extern volatile u32 bzng_x86_cpu_features;

void bzng_init_x86_cpu_features(void);

/*
 * The features of the running CPU, restricted by $BZ2NG_TIER.  Computed on
 * first use; threads that race on it compute and store the same value.
 */
static inline u32 get_x86_cpu_features(void)
{
	if (bzng_x86_cpu_features == 0)
		bzng_init_x86_cpu_features();
	return bzng_x86_cpu_features;
}
#else
static inline u32 get_x86_cpu_features(void) { return 0; }
#endif

#endif /* ARCH_X86_32 || ARCH_X86_64 */

#endif /* LIB_X86_CPU_FEATURES_H */
