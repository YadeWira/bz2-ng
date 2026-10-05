/*
 * x86/kern_avx2.c - the x86-64 AVX2 kernel tier
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * x86-64-v3 (AVX2, BMI1, BMI2, FMA, LZCNT, MOVBE); the tier's CRC kernel is
 * x86/crc32_vpclmul.c on CPUs with VPCLMULQDQ, else x86/crc32_pclmul.c.
 * Compiled with -march=x86-64-v3 on x86-64, and selected only on CPUs with
 * those features.  Compiles to nothing elsewhere, x32 included
 * (BZNG_X86_TIERS).
 */

#include "../lib_common.h"

#if BZNG_X86_TIERS
#  define SUFFIX _avx2
#  include "../kern_template.h"
#else
typedef int bzng_kern_avx2_unused; /* ISO C forbids an empty translation unit */
#endif
