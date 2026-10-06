/*
 * x86/crc32_vpclmul.c - CRC-32/BZIP2 with VPCLMULQDQ
 *
 * CRC-32/BZIP2 by carry-less multiplication with VPCLMULQDQ on 256-bit
 * vectors (x86-64; method as in crc32_pclmul.c, four 256-bit accumulators
 * folded by 1024 bits per step). Compiled with -mavx2 -mvpclmulqdq -mpclmul
 * (cl: /arch:AVX2) for this file only; the dispatch calls it only on CPUs with
 * AVX2 and VPCLMULQDQ. Elsewhere this file compiles to nothing.
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */
#include "../crc32.h"

#if BZNG_X86_TIERS
#  include <immintrin.h>

#  include "crc32_clmul.h"

u32
bzng_crc32_vpclmul(u32 crc, const u8 *p, size_t n)
{
	if (n < 256)
		return bzng_crc32_pclmul(crc, p, n);
	const __m256i rev = _mm256_set_epi8(
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2,
		3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
	const __m256i k1024 = _mm256_set_epi64x(BZNG_K_1088, BZNG_K_1024,
						BZNG_K_1088, BZNG_K_1024);
#  define LD(off)              \
	  _mm256_shuffle_epi8( \
		  _mm256_loadu_si256((const __m256i *)(p + (off))), rev)
#  define FOLD256(a, k)                                          \
	  _mm256_xor_si256(_mm256_clmulepi64_epi128(a, k, 0x11), \
			   _mm256_clmulepi64_epi128(a, k, 0x00))
	__m256i a0 = LD(0), a1 = LD(32), a2 = LD(64), a3 = LD(96);
	/*
	 * The register goes into the top 32 bits of the first block (low lane).
	 */
	a0 = _mm256_xor_si256(a0,
			      _mm256_set_epi32(0, 0, 0, 0, (int)crc, 0, 0, 0));
	p += 128;
	n -= 128;
	while (n >= 128) {
		a0 = _mm256_xor_si256(FOLD256(a0, k1024), LD(0));
		a1 = _mm256_xor_si256(FOLD256(a1, k1024), LD(32));
		a2 = _mm256_xor_si256(FOLD256(a2, k1024), LD(64));
		a3 = _mm256_xor_si256(FOLD256(a3, k1024), LD(96));
		p += 128;
		n -= 128;
	}
#  undef LD
#  undef FOLD256
	/* Eight 128-bit blocks in stream order: a0.lo a0.hi a1.lo ... a3.hi. */
	__m128i blk[8] = {
		_mm256_castsi256_si128(a0), _mm256_extracti128_si256(a0, 1),
		_mm256_castsi256_si128(a1), _mm256_extracti128_si256(a1, 1),
		_mm256_castsi256_si128(a2), _mm256_extracti128_si256(a2, 1),
		_mm256_castsi256_si128(a3), _mm256_extracti128_si256(a3, 1)};
	const __m128i k128 = _mm_set_epi64x(BZNG_K_192, BZNG_K_128);
	__m128i r = blk[0];
	for (int i = 1; i < 8; i++)
		r = _mm_xor_si128(bzng_fold(r, k128), blk[i]);
	while (n >= 16) {
		r = _mm_xor_si128(
			bzng_fold(r, k128),
			bzng_rev16(_mm_loadu_si128((const __m128i *)p)));
		p += 16;
		n -= 16;
	}
	return bzng_clmul_finish(r, p, n);
}
#else
typedef int
	bzng_crc32_vpclmul_unused; /* ISO C forbids an empty translation unit */
#endif
