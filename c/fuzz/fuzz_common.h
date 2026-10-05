/*
 * fuzz_common.h - helpers shared by the fuzz targets
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

#ifndef FUZZ_FUZZ_COMMON_H
#define FUZZ_FUZZ_COMMON_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bzlib.h"
#include "test_tier.h"

/*
 * Reports a finding and aborts, which libFuzzer records as a crash with the
 * input that caused it.
 */
static inline void __attribute__((noreturn, format(printf, 1, 2)))
fuzz_fail(const char *fmt, ...)
{
	va_list ap;

	fputs("FUZZ MISMATCH: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	abort();
}

/*
 * Checks once, before the first input, that $BZ2NG_TIER names the tier the
 * library selected (test/test_tier.h: exit 1 for a name that is no tier, 77
 * for a tier this CPU lacks), and says which tier runs.
 */
static inline void
fuzz_init_tier(const char *target)
{
	require_requested_tier();
	fprintf(stderr, "%s: kernel tier %s\n", target, bzng_tier_name());
}

/*
 * xorshift64 generator for call schedules; *st must not be zero.  Returns the
 * next value.
 */
static inline uint64_t
fuzz_rnd(uint64_t *st)
{
	*st ^= *st << 13;
	*st ^= *st >> 7;
	*st ^= *st << 17;
	return *st;
}

/*
 * Fills a[0..n) and b[0..n) with the same pseudo-random bytes from salt, so
 * that a byte one library writes and the other does not shows up when the
 * buffers are compared.
 */
static inline void
fuzz_fill_pattern(uint8_t *a, uint8_t *b, size_t n, uint64_t salt)
{
	uint64_t x = salt * 0x9E3779B97F4A7C15ull | 1;
	size_t i = 0;

	for (; i + 8 <= n; i += 8) {
		fuzz_rnd(&x);
		memcpy(a + i, &x, 8);
	}
	fuzz_rnd(&x);
	for (; i < n; i++)
		a[i] = (uint8_t)(x >> (8 * (i & 7)));
	memcpy(b, a, n);
}

/* First index in [from, to) at which a and b differ, or to if none. */
static inline size_t
fuzz_first_difference(const uint8_t *a, const uint8_t *b, size_t from,
		      size_t to)
{
	if (to <= from || memcmp(a + from, b + from, to - from) == 0)
		return to;
	while (a[from] == b[from])
		from++;
	return from;
}

/*
 * A counting bzalloc: tracks the bytes in use and the number of calls, and
 * from the fail_at-th call on (1-based; 0 never) returns NULL.
 */
typedef struct {
	long calls, fail_at;
	size_t in_use;
} FuzzAlloc;

/* bzalloc over a FuzzAlloc (opaque). */
static inline void *
fuzz_alloc(void *opaque, int items, int size)
{
	FuzzAlloc *a = opaque;
	const size_t n = (size_t)items * (size_t)size;

	a->calls++;
	if (a->fail_at && a->calls >= a->fail_at)
		return NULL;
	size_t *p = malloc(n + 16);
	if (p == NULL)
		return NULL;
	p[0] = n;
	a->in_use += n;
	return (char *)p + 16;
}

/* bzfree over a FuzzAlloc (opaque). */
static inline void
fuzz_free(void *opaque, void *addr)
{
	FuzzAlloc *a = opaque;

	if (addr == NULL)
		return;
	size_t *p = (size_t *)(void *)((char *)addr - 16);
	a->in_use -= p[0];
	free(p);
}

/* The 64-bit total_in of z. */
static inline uint64_t
fuzz_total_in(const bz_stream *z)
{
	return (uint64_t)z->total_in_hi32 << 32 | z->total_in_lo32;
}

/* The 64-bit total_out of z. */
static inline uint64_t
fuzz_total_out(const bz_stream *z)
{
	return (uint64_t)z->total_out_hi32 << 32 | z->total_out_lo32;
}

#endif /* FUZZ_FUZZ_COMMON_H */
