/*
 * kern.h - the kernels: the ISA-specific routines of the encoder and decoder
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * A kernel tier is one instantiation of kern_template.h compiled with that
 * tier's instruction-set flags: kern_scalar.c (portable C), arm/kern_neon.c
 * (AArch64 NEON), x86/kern_sse4.c (x86-64-v2) and x86/kern_avx2.c
 * (x86-64-v3).  Each defines the five kernels below with the tier's name as a
 * suffix.  Callers reach a kernel through a function pointer that starts at a
 * dispatch function, which selects the best tier the CPU supports, stores
 * that tier's kernel in the pointer and calls it.  Every kernel selects from
 * the same CPU features (restricted by $BZ2NG_TIER), so all of them come from
 * one tier, and every tier produces the same bytes.
 */

#ifndef LIB_KERN_H
#define LIB_KERN_H

#include "lib_common.h"

struct bzng_rle_state;
struct bzng_dstate;
struct bzng_bits;
struct bzng_unrle_io;

/*
 * Encoder input stage: RLE1 of in[0..avail) into the block blk, with the run
 * state and block CRC carried between calls in *st (enc.h).  Returns the
 * number of bytes consumed.  A block is full once a unit brings it to
 * nblock_max or more, so it ends up to 4 bytes past nblock_max, and the
 * kernels store past the block's end as well: 8-byte unit stores in every
 * tier and whole-vector literal copies in the vector tiers, at most 24
 * bytes past nblock_max in all.  The caller's block array (enc.h) has that
 * slack.
 */
typedef size_t (*rle_fill_func_t)(struct bzng_rle_state *st, const u8 *in,
				  size_t avail, u8 *blk, s32 nblock_max);

/*
 * MTF and zero-run coding of the BWT L[0..n), whose bytes are those c with
 * in_use[c] = 1 (the others 0): the values to mtfv (which may be written up to
 * 8 entries past them) and their counts to mtf_freq.  Returns the number of
 * values.
 */
typedef s32 (*mtf_func_t)(const u8 *L, s32 n, const u8 *in_use, u16 *mtfv,
			  s32 *mtf_freq);

/*
 * Least rotation of tt[0..n), where tt holds the block twice plus padding;
 * cand is n ints of scratch.  Sets *periodic when the block is u^m with
 * m > 1.  Returns the start of the least rotation.
 */
typedef s32 (*least_rotation_func_t)(const u8 *tt, s32 n, s32 *cand,
				     int *periodic);

/* Decoder symbol loop from the current symbol boundary (dec.h). */
typedef int (*symbols_func_t)(struct bzng_dstate *s, struct bzng_bits *br);

/* Decoder bulk un-RLE1 from a clean run state (dec.h). */
typedef void (*unrle_func_t)(struct bzng_unrle_io *io);

/* The kernel tiers. */
enum bzng_tier {
	BZNG_TIER_SCALAR,
	BZNG_TIER_NEON,
	BZNG_TIER_SSE4,
	BZNG_TIER_AVX2,
};

/* Declares the kernels of the tier with the given suffix. */
#define DECLARE_TIER_KERNELS(suffix)                                           \
	size_t bzng_rle_fill_##suffix(struct bzng_rle_state *st, const u8 *in, \
				      size_t avail, u8 *blk, s32 nblock_max);  \
	s32 bzng_mtf_##suffix(const u8 *L, s32 n, const u8 *in_use, u16 *mtfv, \
			      s32 *mtf_freq);                                  \
	s32 bzng_least_rotation_##suffix(const u8 *tt, s32 n, s32 *cand,       \
					 int *periodic);                       \
	int bzng_symbols_##suffix(struct bzng_dstate *s,                       \
				  struct bzng_bits *br);                       \
	void bzng_unrle_##suffix(struct bzng_unrle_io *io);

DECLARE_TIER_KERNELS(scalar)

/*
 * Include architecture-specific tiers if available.  The NEON tier and its
 * CRC kernel assume little-endian lane and word order (untested on big
 * endian), so big-endian AArch64 runs the scalar tier, as does x32
 * (BZNG_X86_TIERS).
 */
#undef arch_select_tier
#if BZNG_X86_TIERS
#  include "x86/kern_impl.h"
#elif defined(ARCH_ARM64) && CPU_IS_LITTLE_ENDIAN()
#  include "arm/kern_impl.h"
#endif

#ifndef arch_select_tier
#  define arch_select_tier() BZNG_TIER_SCALAR
#endif

/*
 * arch_select_<kernel>() - the kernel of the best tier the CPU supports, or
 * NULL for the scalar tier.
 */
#ifdef ARCH_SELECT_KERNEL
ARCH_SELECT_KERNEL(rle_fill)
ARCH_SELECT_KERNEL(mtf)
ARCH_SELECT_KERNEL(least_rotation)
ARCH_SELECT_KERNEL(symbols)
ARCH_SELECT_KERNEL(unrle)
#else
#  define arch_select_rle_fill()       NULL
#  define arch_select_mtf()	       NULL
#  define arch_select_least_rotation() NULL
#  define arch_select_symbols()	       NULL
#  define arch_select_unrle()	       NULL
#endif

/* The name of the tier the dispatch selects, for tests and benchmarks. */
const char *bzng_tier_name(void);

#endif /* LIB_KERN_H */
