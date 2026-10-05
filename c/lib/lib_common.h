/*
 * lib_common.h - internal header included by all bz2-ng library code
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

#ifndef LIB_LIB_COMMON_H
#define LIB_LIB_COMMON_H

/*
 * libbz2's private header: bzlib.h with default symbol visibility (so that
 * only its functions are exported from a library built with
 * -fvisibility=hidden), the allocator macros, AssertH, VPrintf and the
 * format constants.
 */
#include "bzlib_private.h"
#include "common_defs.h"

/*
 * Whether the x86-64 kernel tiers and their CRC kernels are built.
 * CMakeLists.txt gives their translation units the tiers' instruction-set
 * flags only where pointers are 8 bytes, so x32 (x86-64 with 4-byte
 * pointers) runs the portable tier.
 */
#if defined(ARCH_X86_64) && UINTPTR_MAX > 0xffffffffu
#  define BZNG_X86_TIERS 1
#else
#  define BZNG_X86_TIERS 0
#endif

/*
 * libbz2's defaults and checks, shared by both drivers (utils.c):
 * bzng_config_ok() is libbz2's bz_config_ok (the type sizes its code
 * assumes); bzng_default_bzalloc and bzng_default_bzfree are the malloc-
 * backed allocator a stream gets when it supplies none.
 */
int bzng_config_ok(void);
void *bzng_default_bzalloc(void *opaque, Int32 items, Int32 size);
void bzng_default_bzfree(void *opaque, void *addr);

/*
 * Runtime assertion support for invariants that hold by construction (the
 * library reports internal errors through libbz2's AssertH).  Compiled in for
 * debug and sanitiser builds only; it may hurt performance significantly.
 */
#ifdef BZNG_ENABLE_ASSERTIONS
NORETURN void bzng_assertion_failed(const char *expr, const char *file,
				    int line);
#  define ASSERT(expr)                                                      \
	  {                                                                 \
		  if (unlikely(!(expr)))                                    \
			  bzng_assertion_failed(#expr, __FILE__, __LINE__); \
	  }
#else
#  define ASSERT(expr) (void)(expr)
#endif

#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b)	  CONCAT_IMPL(a, b)
#define ADD_SUFFIX(name)  CONCAT(name, SUFFIX)

/* FALLTHROUGH - mark an intended fall through to the next case label */
#if __has_attribute(fallthrough)
#  define FALLTHROUGH __attribute__((fallthrough))
#else
#  define FALLTHROUGH \
	  do {        \
	  } while (0)
#endif

/* UNROLL_FULL - fully unroll the following loop (compile-time trip count) */
#if defined(__clang__)
#  define UNROLL_FULL _Pragma("clang loop unroll(full)")
#elif defined(__GNUC__)
#  define UNROLL_FULL _Pragma("GCC unroll 32")
#else
#  define UNROLL_FULL
#endif

/* popcount32(v) - the number of 1 bits in v */
static forceinline unsigned
popcount32(u32 v)
{
#if defined(__GNUC__) || __has_builtin(__builtin_popcount)
	return (unsigned)__builtin_popcount(v);
#else
	v = v - ((v >> 1) & 0x55555555);
	v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
	return (((v + (v >> 4)) & 0x0f0f0f0f) * 0x01010101) >> 24;
#endif
}

/* zero_bytes(v) - 0x80 in every zero byte of v, exactly (no false positives) */
static forceinline u64
zero_bytes(u64 v)
{
	const u64 lo7 = 0x7f7f7f7f7f7f7f7f;

	return ~(((v & lo7) + lo7) | v | lo7);
}

/*
 * State objects are allocated through the caller's bzalloc, which libbz2 only
 * ever needed to return 4-byte-aligned memory.  They are over-allocated by
 * STATE_ALIGN - 1 bytes and aligned with align_state().
 */
#define STATE_ALIGN 16

static forceinline void *
align_state(void *raw)
{
	return (void *)(((uintptr_t)raw + STATE_ALIGN - 1) &
			~(uintptr_t)(STATE_ALIGN - 1));
}

#endif /* LIB_LIB_COMMON_H */
