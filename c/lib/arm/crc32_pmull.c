/*
 * arm/crc32_pmull.c - CRC-32/BZIP2 with PMULL
 *
 * CRC-32/BZIP2 by carry-less multiplication with PMULL (AArch64), the method
 * of x86/crc32_pclmul.c with PMULL in place of PCLMULQDQ and the same
 * constants: each 16-byte block is byte-reversed, so that no bit reflection
 * is needed, and eight accumulators are folded forward by 1,024 bits, which
 * keeps enough multiplications in flight on Apple's cores and Neoverse V2.
 * The 128-bit residual and the tail are finished by the CRC32-instruction
 * kernel (arm/crc32_armv8.c), as are inputs too short to fold.  Compiled with
 * -march=armv8-a+crc+crypto for this file only; the dispatch calls it only on
 * CPUs with PMULL and the CRC32 instructions.  Elsewhere, and on big-endian
 * AArch64 (kern.h), this file compiles to nothing.
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */
#include "../crc32.h"

#if defined(ARCH_ARM64) && CPU_IS_LITTLE_ENDIAN()
#  include <arm_acle.h>
#  include <arm_neon.h>

/* A folding constant pair: x^D mod P in the low lane, x^(D+64) mod P high. */
static forceinline poly64x2_t
fold_consts(u64 k_d, u64 k_d64)
{
	const u64 k[2] = {k_d, k_d64};

	return vreinterpretq_p64_u64(vld1q_u64(k));
}

/* The 16 bytes at p in reverse order: bit i is the coefficient of x^i. */
static forceinline uint8x16_t
load_rev16(const u8 *p, uint8x16_t rev)
{
	return vqtbl1q_u8(vld1q_u8(p), rev);
}

/* acc * x^D + d (mod P), each product at most 96 bits. */
static forceinline uint8x16_t
fold(uint8x16_t acc, poly64x2_t k, uint8x16_t d)
{
	const poly64x2_t a = vreinterpretq_p64_u8(acc);
	const uint8x16_t lo = vreinterpretq_u8_p128(
		vmull_p64(vgetq_lane_p64(a, 0), vgetq_lane_p64(k, 0)));
	const uint8x16_t hi = vreinterpretq_u8_p128(vmull_high_p64(a, k));

	return veorq_u8(veorq_u8(lo, hi), d);
}

u32
bzng_crc32_pmull(u32 crc, const u8 *p, size_t n)
{
	static const u8 rev_index[16] = {15, 14, 13, 12, 11, 10, 9, 8,
					 7,  6,	 5,  4,	 3,  2,	 1, 0};

	/*
	 * Below 256 bytes folding is no faster than the CRC32 instructions on
	 * Neoverse V2 (it is from 128 on Apple's cores).
	 */
	if (n < 256)
		return bzng_crc32_armv8(crc, p, n);
	const uint8x16_t rev = vld1q_u8(rev_index);
	const poly64x2_t k1024 = fold_consts(BZNG_K_1024, BZNG_K_1088),
			 k512 = fold_consts(BZNG_K_512, BZNG_K_576),
			 k128 = fold_consts(BZNG_K_128, BZNG_K_192);
	uint8x16_t a0 = load_rev16(p, rev), a1 = load_rev16(p + 16, rev),
		   a2 = load_rev16(p + 32, rev), a3 = load_rev16(p + 48, rev),
		   a4 = load_rev16(p + 64, rev), a5 = load_rev16(p + 80, rev),
		   a6 = load_rev16(p + 96, rev), a7 = load_rev16(p + 112, rev);
	/* The register enters as an XOR into the first 32 message bits. */
	a0 = veorq_u8(a0, vreinterpretq_u8_u32(
				  vsetq_lane_u32(crc, vdupq_n_u32(0), 3)));
	p += 128;
	n -= 128;
	while (n >= 128) {
		a0 = fold(a0, k1024, load_rev16(p, rev));
		a1 = fold(a1, k1024, load_rev16(p + 16, rev));
		a2 = fold(a2, k1024, load_rev16(p + 32, rev));
		a3 = fold(a3, k1024, load_rev16(p + 48, rev));
		a4 = fold(a4, k1024, load_rev16(p + 64, rev));
		a5 = fold(a5, k1024, load_rev16(p + 80, rev));
		a6 = fold(a6, k1024, load_rev16(p + 96, rev));
		a7 = fold(a7, k1024, load_rev16(p + 112, rev));
		p += 128;
		n -= 128;
	}
	a0 = fold(a0, k512, a4);
	a1 = fold(a1, k512, a5);
	a2 = fold(a2, k512, a6);
	a3 = fold(a3, k512, a7);
	if (n >= 64) {
		a0 = fold(a0, k512, load_rev16(p, rev));
		a1 = fold(a1, k512, load_rev16(p + 16, rev));
		a2 = fold(a2, k512, load_rev16(p + 32, rev));
		a3 = fold(a3, k512, load_rev16(p + 48, rev));
		p += 64;
		n -= 64;
	}
	uint8x16_t r =
		fold(a0, fold_consts(BZNG_K_384, BZNG_K_448), vdupq_n_u8(0));
	r = fold(a1, fold_consts(BZNG_K_256, BZNG_K_320), r);
	r = fold(a2, k128, veorq_u8(r, a3));
	while (n >= 16) {
		r = fold(r, k128, load_rev16(p, rev));
		p += 16;
		n -= 16;
	}
	/*
	 * The register after the prefix is r * x^32 mod P: the register of r's
	 * 16 bytes, most significant first, from a zero register.  The CRC32
	 * instructions take the bit-reversed register and message (as in
	 * arm/crc32_armv8.c), and bit-reversing each half of r gives its bytes
	 * in message order with their bits reversed.
	 */
	const uint64x2_t r64 = vreinterpretq_u64_u8(r);
	const u32 reg = __crc32d(__crc32d(0, __rbitll(vgetq_lane_u64(r64, 1))),
				 __rbitll(vgetq_lane_u64(r64, 0)));
	return bzng_crc32_armv8(__rbit(reg), p, n);
}
#else
typedef int
	bzng_crc32_pmull_unused; /* ISO C forbids an empty translation unit */
#endif
