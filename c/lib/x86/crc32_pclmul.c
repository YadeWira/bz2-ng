/*
 * x86/crc32_pclmul.c - CRC-32/BZIP2 with PCLMULQDQ
 *
 * CRC-32/BZIP2 by carry-less multiplication with PCLMULQDQ (x86-64).
 * Compiled with -msse4.1 -mpclmul for this file only; the dispatch calls it
 * only on CPUs with both. Elsewhere this file compiles to nothing.
 *
 * bzip2's CRC is MSB-first, so no bit reflection is needed: each 16-byte
 * block is byte-reversed, after which bit i of the 128-bit lane is the
 * coefficient of x^i of the block polynomial and clmul is plain polynomial
 * multiplication. Accumulators are folded forward by D bits as
 * A * x^D = A_hi * (x^(D+64) mod P) + A_lo * (x^D mod P)   (mod P),
 * each product at most 96 bits. The final 128-bit residual R is congruent to
 * the whole prefix, so the register after the prefix is R * x^32 mod P, which
 * the table kernel computes from R's 16 bytes with a zero start register; the
 * tail (< 16 bytes) follows with the table kernel too. The register enters as
 * an XOR into the first 32 message bits.
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */
#include "../crc32.h"

#ifdef ARCH_X86_64
#  include <immintrin.h>

#  include "crc32_clmul.h"

u32
bzng_crc32_pclmul(u32 crc, const u8 *p, size_t n)
{
	if (n < 64)
		return bzng_crc32_slice16(crc, p, n);
	const __m128i k512 = _mm_set_epi64x(BZNG_K_576, BZNG_K_512),
		      k128 = _mm_set_epi64x(BZNG_K_192, BZNG_K_128);
	__m128i a0 = bzng_rev16(_mm_loadu_si128((const __m128i *)p));
	__m128i a1 = bzng_rev16(_mm_loadu_si128((const __m128i *)(p + 16)));
	__m128i a2 = bzng_rev16(_mm_loadu_si128((const __m128i *)(p + 32)));
	__m128i a3 = bzng_rev16(_mm_loadu_si128((const __m128i *)(p + 48)));
	a0 = _mm_xor_si128(a0, _mm_set_epi32((int)crc, 0, 0, 0));
	p += 64;
	n -= 64;
	while (n >= 64) {
		a0 = _mm_xor_si128(
			bzng_fold(a0, k512),
			bzng_rev16(_mm_loadu_si128((const __m128i *)p)));
		a1 = _mm_xor_si128(
			bzng_fold(a1, k512),
			bzng_rev16(_mm_loadu_si128((const __m128i *)(p + 16))));
		a2 = _mm_xor_si128(
			bzng_fold(a2, k512),
			bzng_rev16(_mm_loadu_si128((const __m128i *)(p + 32))));
		a3 = _mm_xor_si128(
			bzng_fold(a3, k512),
			bzng_rev16(_mm_loadu_si128((const __m128i *)(p + 48))));
		p += 64;
		n -= 64;
	}
	__m128i r = _mm_xor_si128(
		bzng_fold(a0, _mm_set_epi64x(BZNG_K_448, BZNG_K_384)),
		bzng_fold(a1, _mm_set_epi64x(BZNG_K_320, BZNG_K_256)));
	r = _mm_xor_si128(r, _mm_xor_si128(bzng_fold(a2, k128), a3));
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
	bzng_crc32_pclmul_unused; /* ISO C forbids an empty translation unit */
#endif
