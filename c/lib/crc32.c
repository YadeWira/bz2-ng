/*
 * crc32.c - CRC-32/BZIP2: table kernels and run-time dispatch
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

#include "crc32.h"
#include "kern.h"

u32
bzng_crc32_slice16(u32 crc, const u8 *p, size_t len)
{
	const u32(*t)[256] = bzng_crc32_table;

	while (len >= 16) {
		u32 a = crc ^ get_unaligned_be32(p);
		u32 b = get_unaligned_be32(p + 4);
		u32 c = get_unaligned_be32(p + 8);
		u32 d = get_unaligned_be32(p + 12);

		crc = t[15][a >> 24] ^ t[14][(a >> 16) & 0xff] ^
		      t[13][(a >> 8) & 0xff] ^ t[12][a & 0xff] ^
		      t[11][b >> 24] ^ t[10][(b >> 16) & 0xff] ^
		      t[9][(b >> 8) & 0xff] ^ t[8][b & 0xff] ^ t[7][c >> 24] ^
		      t[6][(c >> 16) & 0xff] ^ t[5][(c >> 8) & 0xff] ^
		      t[4][c & 0xff] ^ t[3][d >> 24] ^ t[2][(d >> 16) & 0xff] ^
		      t[1][(d >> 8) & 0xff] ^ t[0][d & 0xff];
		p += 16;
		len -= 16;
	}
	while (len--)
		crc = crc32_byte(crc, *p++);
	return crc;
}

/* Include architecture-specific implementation(s) if available. */
#undef DEFAULT_IMPL
#undef arch_select_crc32_func
#if defined(ARCH_ARM64)
#  include "arm/crc32_impl.h"
#elif defined(ARCH_X86_64)
#  include "x86/crc32_impl.h"
#endif

#ifndef DEFAULT_IMPL
#  define DEFAULT_IMPL bzng_crc32_slice16
#endif

#ifdef arch_select_crc32_func
static u32 dispatch_crc32(u32 crc, const u8 *p, size_t len);

static volatile crc32_func_t crc32_impl = dispatch_crc32;

/* Choose the best implementation at runtime. */
static u32
dispatch_crc32(u32 crc, const u8 *p, size_t len)
{
	crc32_func_t f = arch_select_crc32_func();

	if (f == NULL)
		f = DEFAULT_IMPL;

	crc32_impl = f;
	return f(crc, p, len);
}
#else
/* The best implementation is statically known, so call it directly. */
#  define crc32_impl DEFAULT_IMPL
#endif

u32
bzng_crc32(u32 crc, const u8 *p, size_t len)
{
	return crc32_impl(crc, p, len);
}
