/*
 * utils.c - utility functions for bz2-ng
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

#include "kern.h"
#include "lib_common.h"

int
bzng_config_ok(void)
{
	return sizeof(int) == 4 && sizeof(short) == 2 && sizeof(char) == 1;
}

void *
bzng_default_bzalloc(void *opaque, Int32 items, Int32 size)
{
	(void)opaque;
	return malloc((size_t)items * (size_t)size);
}

void
bzng_default_bzfree(void *opaque, void *addr)
{
	(void)opaque;
	if (addr != NULL)
		free(addr);
}

#ifdef BZNG_ENABLE_ASSERTIONS
NORETURN void
bzng_assertion_failed(const char *expr, const char *file, int line)
{
#  ifdef BZ_NO_STDIO
	(void)expr;
	(void)file;
	(void)line;
	bz_internal_error(5003);
#  else
	fprintf(stderr, "bz2-ng: assertion failed: %s at %s:%d\n", expr, file,
		line);
#  endif
	abort();
}
#endif

/* The tier the kernel dispatch selects on this CPU (kern.h). */
const char *
bzng_tier_name(void)
{
	switch (arch_select_tier()) {
	case BZNG_TIER_NEON:
		return "neon";
	case BZNG_TIER_SSE4:
		return "sse4";
	case BZNG_TIER_AVX2:
		return "avx2";
	default:
		return "scalar";
	}
}
