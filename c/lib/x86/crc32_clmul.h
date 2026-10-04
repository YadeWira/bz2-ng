/*
 * x86/crc32_clmul.h - helpers of the carry-less CRC kernels
 *
 * Folding constants and helpers shared by the two carry-less CRC kernels
 * (crc32_pclmul.c, crc32_vpclmul.c); see crc32_pclmul.c for the method. Include
 * only from translation units compiled with PCLMULQDQ and SSE4.1 enabled.
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */
#ifndef LIB_X86_CRC32_CLMUL_H
#define LIB_X86_CRC32_CLMUL_H

#include <immintrin.h>

#include "../crc32.h"

/* x^D mod P for the folding distances used (P = 0x104c11db7). */
#define BZNG_K_576  0x8833794cu
#define BZNG_K_512  0xe6228b11u
#define BZNG_K_448  0x64bf7a9bu
#define BZNG_K_384  0x8c3828a8u
#define BZNG_K_320  0x569700e5u
#define BZNG_K_256  0x75be46b7u
#define BZNG_K_192  0xc5b9cd4cu
#define BZNG_K_128  0xe8a45605u
#define BZNG_K_1088 0x10bd4d7cu
#define BZNG_K_1024 0x567fddebu

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
