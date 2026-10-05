/*
 * x86/kern_sse4.c - the x86-64 SSE4 kernel tier
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * x86-64-v2 (SSE4.2, SSSE3, POPCNT); the tier's CRC kernel is
 * x86/crc32_pclmul.c.  Compiled with -march=x86-64-v2 on x86-64, and selected
 * only on CPUs with those features.  Compiles to nothing elsewhere, x32
 * included (BZNG_X86_TIERS).
 */

#include "../lib_common.h"

#if BZNG_X86_TIERS
#  define SUFFIX _sse4
#  include "../kern_template.h"
#else
typedef int bzng_kern_sse4_unused; /* ISO C forbids an empty translation unit */
#endif
