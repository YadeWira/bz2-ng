/*
 * crc32.h - CRC-32/BZIP2 (MSB-first, polynomial 0x04c11db7)
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

#ifndef LIB_CRC32_H
#define LIB_CRC32_H

#include "lib_common.h"

/*
 * Every function here updates a non-inverted register: a block CRC is
 * ~bzng_crc32(0xffffffff, data, n).
 */
typedef u32 (*crc32_func_t)(u32 crc, const u8 *p, size_t len);

/*
 * bzng_crc32_table[0][b] is the register effect of byte b (libbz2's
 * BZ2_crc32Table); bzng_crc32_table[k][b] that of byte b followed by k zero
 * bytes, for slicing.
 */
extern const u32 bzng_crc32_table[16][256];

/* The register after one byte b. */
static forceinline u32
crc32_byte(u32 crc, u8 b)
{
	return (crc << 8) ^ bzng_crc32_table[0][(crc >> 24) ^ b];
}

/* The register after p[0..len), with the best kernel the CPU supports. */
u32 bzng_crc32(u32 crc, const u8 *p, size_t len);

/* Table kernel, any CPU: sixteen bytes per step. */
u32 bzng_crc32_slice16(u32 crc, const u8 *p, size_t len);

/*
 * Instruction-set kernels, each in its own translation unit and only called
 * on CPUs with the instructions: AArch64 CRC32 (arm/crc32_armv8.c), AArch64
 * PMULL with CRC32 (arm/crc32_pmull.c), x86 PCLMULQDQ with SSE4.1
 * (x86/crc32_pclmul.c), and VPCLMULQDQ on 256-bit vectors with AVX2
 * (x86/crc32_vpclmul.c).
 */
u32 bzng_crc32_armv8(u32 crc, const u8 *p, size_t len);
u32 bzng_crc32_pmull(u32 crc, const u8 *p, size_t len);
u32 bzng_crc32_pclmul(u32 crc, const u8 *p, size_t len);
u32 bzng_crc32_vpclmul(u32 crc, const u8 *p, size_t len);

/*
 * x^D mod P for the folding distances of the carry-less kernels
 * (P = 0x104c11db7; x86/crc32_pclmul.c describes the method).
 */
#define BZNG_K_1088 0x10bd4d7cu
#define BZNG_K_1024 0x567fddebu
#define BZNG_K_576  0x8833794cu
#define BZNG_K_512  0xe6228b11u
#define BZNG_K_448  0x64bf7a9bu
#define BZNG_K_384  0x8c3828a8u
#define BZNG_K_320  0x569700e5u
#define BZNG_K_256  0x75be46b7u
#define BZNG_K_192  0xc5b9cd4cu
#define BZNG_K_128  0xe8a45605u

#endif /* LIB_CRC32_H */
