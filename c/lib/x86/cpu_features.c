/*
 * x86/cpu_features.c - feature detection for x86 CPUs
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
 * Adapted from libdeflate 1.26 (lib/x86/cpu_features.c) for bz2-ng: detects
 * the features of x86-64-v2 and -v3 plus PCLMULQDQ and VPCLMULQDQ, applies
 * the BZ2NG_TIER override, and has no AVX-512 detection.
 */

#include "../cpu_features_common.h" /* must be included first */
#include "cpu_features.h"

#ifdef X86_CPU_FEATURES_KNOWN
/* Runtime x86 CPU feature detection is supported. */

#ifdef _MSC_VER
#  include <intrin.h>
#endif

/* Execute the CPUID instruction. */
static inline void
cpuid(u32 leaf, u32 subleaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
#ifdef _MSC_VER
	int result[4];

	__cpuidex(result, leaf, subleaf);
	*a = result[0];
	*b = result[1];
	*c = result[2];
	*d = result[3];
#else
	__asm__ volatile("cpuid" : "=a" (*a), "=b" (*b), "=c" (*c), "=d" (*d)
			 : "a" (leaf), "c" (subleaf));
#endif
}

/* Read an extended control register. */
static inline u64
read_xcr(u32 index)
{
#ifdef _MSC_VER
	return _xgetbv(index);
#else
	u32 d, a;

	/*
	 * Execute the "xgetbv" instruction.  Old versions of binutils do not
	 * recognize this instruction, so list the raw bytes instead.
	 *
	 * This must be 'volatile' to prevent this code from being moved out
	 * from under the check for OSXSAVE.
	 */
	__asm__ volatile(".byte 0x0f, 0x01, 0xd0" :
			 "=d" (d), "=a" (a) : "c" (index));

	return ((u64)d << 32) | a;
#endif
}

/* The tiers BZ2NG_TIER can name, and the features each may use. */
static const struct tier_features x86_tiers[] = {
	{"scalar",	0},
	{"sse4",	X86_CPU_FEATURES_V2 | X86_CPU_FEATURE_PCLMULQDQ},
	{"avx2",	~0U},
};

volatile u32 bzng_x86_cpu_features = 0;

/* Initialize bzng_x86_cpu_features. */
void bzng_init_x86_cpu_features(void)
{
	u32 max_leaf, max_ext_leaf;
	u32 manufacturer[3];
	u32 a, b, c, d;
	u64 xcr0 = 0;
	u32 features = 0;
	bool ymm_ok;

	/* EAX=0: Highest Function Parameter and Manufacturer ID */
	cpuid(0, 0, &max_leaf, &manufacturer[0], &manufacturer[2],
	      &manufacturer[1]);
	if (max_leaf < 1)
		goto out;

	/* EAX=1: Processor Info and Feature Bits */
	cpuid(1, 0, &a, &b, &c, &d);
	if (d & (1 << 26))
		features |= X86_CPU_FEATURE_SSE2;
	if (c & (1 << 9))
		features |= X86_CPU_FEATURE_SSSE3;
	if (c & (1 << 19))
		features |= X86_CPU_FEATURE_SSE4_1;
	if (c & (1 << 20))
		features |= X86_CPU_FEATURE_SSE4_2;
	if (c & (1 << 23))
		features |= X86_CPU_FEATURE_POPCNT;
	if (c & (1 << 13))
		features |= X86_CPU_FEATURE_CX16;
	if (c & (1 << 22))
		features |= X86_CPU_FEATURE_MOVBE;
	/*
	 * No known CPUs have pclmulqdq without sse4.1, so in practice code
	 * targeting pclmulqdq can use sse4.1 instructions.  But to be safe,
	 * explicitly check for both the pclmulqdq and sse4.1 bits.
	 */
	if ((c & (1 << 1)) && (c & (1 << 19)))
		features |= X86_CPU_FEATURE_PCLMULQDQ;
	if (c & (1 << 27))
		xcr0 = read_xcr(0);
	ymm_ok = (xcr0 & 0x6) == 0x6;
	if (ymm_ok) {
		if (c & (1 << 28))
			features |= X86_CPU_FEATURE_AVX;
		if (c & (1 << 12))
			features |= X86_CPU_FEATURE_FMA;
		if (c & (1 << 29))
			features |= X86_CPU_FEATURE_F16C;
	}

	/* EAX=0x80000001: Extended Processor Info and Feature Bits */
	cpuid(0x80000000, 0, &max_ext_leaf, &b, &c, &d);
	if (max_ext_leaf >= 0x80000001) {
		cpuid(0x80000001, 0, &a, &b, &c, &d);
		if (c & (1 << 0))
			features |= X86_CPU_FEATURE_LAHF;
		if (c & (1 << 5))
			features |= X86_CPU_FEATURE_LZCNT;
	}

	if (max_leaf < 7)
		goto out;

	/* EAX=7, ECX=0: Extended Features */
	cpuid(7, 0, &a, &b, &c, &d);
	if (b & (1 << 3))
		features |= X86_CPU_FEATURE_BMI1;
	if (b & (1 << 8))
		features |= X86_CPU_FEATURE_BMI2;
	if (ymm_ok) {
		if (b & (1 << 5))
			features |= X86_CPU_FEATURE_AVX2;
		if (c & (1 << 10))
			features |= X86_CPU_FEATURE_VPCLMULQDQ;
	}

out:
	apply_tier_override(&features, x86_tiers, ARRAY_LEN(x86_tiers));

	bzng_x86_cpu_features = features | X86_CPU_FEATURES_KNOWN;
}

#else
typedef int bzng_x86_cpu_features_unused; /* ISO C forbids an empty TU */
#endif /* X86_CPU_FEATURES_KNOWN */
