/*
 * x86/crc32_clmul.h - helpers of the carry-less CRC kernels
 *
 * Helpers shared by the two carry-less CRC kernels (crc32_pclmul.c,
 * crc32_vpclmul.c), whose folding constants are in ../crc32.h; see
 * crc32_pclmul.c for the method. Include only from translation units
 * compiled with PCLMULQDQ and SSE4.1 enabled.
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */
#ifndef LIB_X86_CRC32_CLMUL_H
#define LIB_X86_CRC32_CLMUL_H

#include <immintrin.h>

#include "../crc32.h"

/* The 16 bytes of v in reverse order. */
static inline __m128i
bzng_rev16(__m128i v)
{
	return _mm_shuffle_epi8(v, _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
						10, 11, 12, 13, 14, 15));
}

/*
 * acc * x^D mod P: k holds (x^(D+64) mod P) in the high qword, (x^D mod P)
 * in the low one.
 */
static inline __m128i
bzng_fold(__m128i acc, __m128i k)
{
	return _mm_xor_si128(_mm_clmulepi64_si128(acc, k, 0x11),
			     _mm_clmulepi64_si128(acc, k, 0x00));
}

/*
 * Register after the prefix whose 128-bit residual is r, then the n-byte
 * tail p.
 */
static inline u32
bzng_clmul_finish(__m128i r, const u8 *p, size_t n)
{
	u8 bytes[16];
	_mm_storeu_si128((__m128i *)bytes, bzng_rev16(r));
	return bzng_crc32_slice16(bzng_crc32_slice16(0, bytes, 16), p, n);
}

#endif /* LIB_X86_CRC32_CLMUL_H */
