/*
 * arm/crc32_armv8.c - CRC-32/BZIP2 with the AArch64 CRC32 instructions
 *
 * CRC-32/BZIP2 with the AArch64 CRC32 instructions. Compiled with the CRC
 * extension enabled for this file only (-march=armv8-a+crc); the dispatch
 * calls it only when the CPU reports the instructions. Elsewhere, and on
 * big-endian AArch64 (kern.h), this file compiles to nothing.
 *
 * The instructions compute the reflected CRC-32 with the same polynomial, so
 * the register is bit-reversed on entry and exit and the bits of every input
 * byte are reversed (rbit reverses the whole word, rev restores byte order).
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */
#include "../crc32.h"

#if defined(ARCH_ARM64) && CPU_IS_LITTLE_ENDIAN()
#  include <arm_acle.h>

/* x with the bits of every byte reversed and the byte order kept. */
static inline u64
rev_bits_in_bytes(u64 x)
{
	return __revll(__rbitll(x));
}

u32
bzng_crc32_armv8(u32 crc, const u8 *p, size_t n)
{
	u32 r = __rbit(crc);
	while (n >= 32) {
		u64 x0 = load_u64_unaligned(p), x1 = load_u64_unaligned(p + 8),
		    x2 = load_u64_unaligned(p + 16),
		    x3 = load_u64_unaligned(p + 24);
		r = __crc32d(r, rev_bits_in_bytes(x0));
		r = __crc32d(r, rev_bits_in_bytes(x1));
		r = __crc32d(r, rev_bits_in_bytes(x2));
		r = __crc32d(r, rev_bits_in_bytes(x3));
		p += 32;
		n -= 32;
	}
	while (n >= 8) {
		r = __crc32d(r, rev_bits_in_bytes(load_u64_unaligned(p)));
		p += 8;
		n -= 8;
	}
	while (n--)
		r = __crc32b(r, (u8)(__rbit(*p++) >> 24));
	return __rbit(r);
}
#else
typedef int
	bzng_crc32_armv8_unused; /* ISO C forbids an empty translation unit */
#endif
