/*
 * dec_bigbuf.c - decompression from one input buffer longer than PTRDIFF_MAX
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * A 32-bit process can hand BZ2_bzDecompress one input buffer of more than
 * 2 GiB (an i386 process under a 64-bit kernel has almost 4 GiB of address
 * space), so the distance from next_in to the end of its input exceeds
 * PTRDIFF_MAX.  This maps an anonymous region of 2 GiB + 64 MiB without
 * reserving memory, writes a generated stream at its start, and decodes it
 * with avail_in set to the whole region, driving stock libbz2 with the same
 * calls: after every call the return code, next_in and next_out (as
 * offsets), avail_in, avail_out and the totals must agree, and at the end
 * the whole output buffers.  Each stream is decoded with all the output
 * space at once and in output pieces, so that every call starts with more
 * than PTRDIFF_MAX bytes of input, in both decoder layouts and in small mode.
 *
 * Reports itself skipped (exit 77) where pointers are 64-bit, where there is
 * no mmap, and where the region cannot be mapped.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bzlib.h"
#include "dec.h"
#include "reference_api.h"

#if defined(_WIN32)
int
main(void)
{
	printf("no mmap on this platform: skipped\n");
	return 77;
}
#else
#  include <sys/mman.h>
#  ifndef MAP_ANONYMOUS
#    define MAP_ANONYMOUS MAP_ANON
#  endif
#  ifndef MAP_NORESERVE
#    define MAP_NORESERVE 0
#  endif

/* The region: more than PTRDIFF_MAX on a 32-bit target. */
#  define REGION_BYTES ((size_t)0x84000000u)
/* Output bytes past the stream's end, which neither library may touch. */
#  define OUT_SLACK    64

static int failures;
static uint64_t rs = 0x9E3779B97F4A7C15ull;

/* xorshift64 */
static uint64_t
rnd(void)
{
	rs ^= rs << 13;
	rs ^= rs >> 7;
	rs ^= rs << 17;
	return rs;
}

/* Text-like data with a small dictionary and some long runs. */
static void
make_input(uint8_t *b, size_t n)
{
	static const char *const words[] = {
		"the ",	 "quick ", "brown ", "fox ",   "jumps ",   "over ",
		"lazy ", "dog ",   "bzip2 ", "block ", "sorting ", "\n"};
	size_t i = 0;
	while (i < n) {
		const uint64_t r = rnd();
		if (r % 50 == 0) {
			size_t len = 4 + (size_t)(r >> 8) % 600;
			if (len > n - i)
				len = n - i;
			memset(b + i, (int)(r >> 32), len);
			i += len;
			continue;
		}
		const char *w = words[(r >> 16) % 12];
		size_t len = strlen(w);
		if (len > n - i)
			len = n - i;
		memcpy(b + i, w, len);
		i += len;
	}
}

/* Offset of p from base, for pointers into one region of any length. */
static uintptr_t
offset(const void *p, const void *base)
{
	return (uintptr_t)p - (uintptr_t)base;
}

/*
 * Compares the two streams' observable fields after call number call;
 * returns 0 if they agree.
 */
static int
same_state(const char *name, long call, int ra, int rb, const bz_stream *a,
	   const bz_stream *b, const uint8_t *in, const uint8_t *out_a,
	   const uint8_t *out_b)
{
	if (ra == rb && offset(a->next_in, in) == offset(b->next_in, in) &&
	    a->avail_in == b->avail_in &&
	    offset(a->next_out, out_a) == offset(b->next_out, out_b) &&
	    a->avail_out == b->avail_out &&
	    a->total_in_lo32 == b->total_in_lo32 &&
	    a->total_in_hi32 == b->total_in_hi32 &&
	    a->total_out_lo32 == b->total_out_lo32 &&
	    a->total_out_hi32 == b->total_out_hi32)
		return 0;
	printf("MISMATCH %s call %ld: rc %d / %d, next_in +%lu / +%lu, "
	       "avail_in %u / %u, next_out +%lu / +%lu, avail_out %u / %u, "
	       "total_in %u / %u, total_out %u / %u (libbz2 / bz2-ng)\n",
	       name, call, ra, rb, (unsigned long)offset(a->next_in, in),
	       (unsigned long)offset(b->next_in, in), a->avail_in, b->avail_in,
	       (unsigned long)offset(a->next_out, out_a),
	       (unsigned long)offset(b->next_out, out_b), a->avail_out,
	       b->avail_out, a->total_in_lo32, b->total_in_lo32,
	       a->total_out_lo32, b->total_out_lo32);
	return 1;
}

/*
 * Decodes the stream at the start of in (in_len bytes in all) with libbz2 and
 * with bz2-ng in the given layout and small setting, offering at most piece
 * output bytes per call (0: all of it), and compares them after every call.
 */
static void
decode_both(const char *name, const uint8_t *in, size_t in_len, size_t n,
	    int layout, int small, size_t piece)
{
	const size_t cap = n + OUT_SLACK;
	uint8_t *out_a = malloc(cap), *out_b = malloc(cap);
	if (out_a == NULL || out_b == NULL) {
		printf("FAIL %s: out of memory\n", name);
		failures++;
		free(out_a);
		free(out_b);
		return;
	}
	memset(out_a, 0xA5, cap);
	memset(out_b, 0xA5, cap);
	bz_stream a, b;
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	const int ia = ref_BZ2_bzDecompressInit(&a, 0, small);
	const int ib = bzng_decompress_init(&b, 0, small, layout);
	if (ia != BZ_OK || ib != BZ_OK) {
		printf("FAIL %s: init %d / %d\n", name, ia, ib);
		failures++;
		free(out_a);
		free(out_b);
		return;
	}
	a.next_in = b.next_in = (char *)in;
	a.avail_in = b.avail_in = (unsigned int)in_len;
	a.next_out = (char *)out_a;
	b.next_out = (char *)out_b;
	int ra = BZ_OK, rb = BZ_OK;
	long call = 0;
	while (ra == BZ_OK && rb == BZ_OK && (size_t)call <= cap) {
		if (in_len - offset(b.next_in, in) <= (size_t)PTRDIFF_MAX) {
			printf("FAIL %s: call %ld would start with only %u "
			       "input bytes\n",
			       name, call + 1, b.avail_in);
			failures++;
			break;
		}
		const size_t left = cap - offset(a.next_out, out_a);
		const size_t give = piece && piece < left ? piece : left;
		a.avail_out = b.avail_out = (unsigned int)give;
		ra = ref_BZ2_bzDecompress(&a);
		rb = BZ2_bzDecompress(&b);
		call++;
		if (same_state(name, call, ra, rb, &a, &b, in, out_a, out_b)) {
			failures++;
			break;
		}
	}
	if (ra != BZ_STREAM_END) {
		printf("FAIL %s: libbz2 returned %d, not BZ_STREAM_END\n", name,
		       ra);
		failures++;
	} else if (memcmp(out_a, out_b, cap) != 0) {
		printf("FAIL %s: output buffers differ\n", name);
		failures++;
	}
	ref_BZ2_bzDecompressEnd(&a);
	BZ2_bzDecompressEnd(&b);
	free(out_a);
	free(out_b);
}

/*
 * Compresses n generated bytes with libbz2 at level into the start of region
 * and decodes the stream with avail_in set to the whole region, in both
 * layouts and both small settings, with all the output space at once and in
 * pieces (one byte at a time in the wide layout only, for time).
 */
static void
check_stream(uint8_t *region, size_t n, int level)
{
	static const size_t pieces[] = {0, 1, 4093, 65536};
	uint8_t *src = malloc(n);
	unsigned int comp_len = (unsigned int)(n + n / 100 + 600);
	if (src == NULL) {
		printf("FAIL: out of memory\n");
		failures++;
		return;
	}
	make_input(src, n);
	const int rc = ref_BZ2_bzBuffToBuffCompress(
		(char *)region, &comp_len, (char *)src, (unsigned int)n, level,
		0, 0);
	free(src);
	if (rc != BZ_OK) {
		printf("FAIL: compression returned %d\n", rc);
		failures++;
		return;
	}
	for (int layout = BZNG_LAYOUT_FIT; layout <= BZNG_LAYOUT_WIDE;
	     layout++) {
		for (int small = 0; small <= 1; small++) {
			for (size_t j = 0; j < sizeof pieces / sizeof pieces[0];
			     j++) {
				if (pieces[j] == 1 &&
				    (small || layout != BZNG_LAYOUT_WIDE))
					continue;
				char name[96];
				snprintf(name, sizeof name,
					 "%lu bytes at level %d, layout %d, "
					 "small %d, pieces %lu",
					 (unsigned long)n, level, layout, small,
					 (unsigned long)pieces[j]);
				decode_both(name, region, REGION_BYTES, n,
					    layout, small, pieces[j]);
			}
		}
	}
	printf("%lu bytes at level %d: stream of %u bytes decoded\n",
	       (unsigned long)n, level, comp_len);
	memset(region, 0, comp_len);
}

int
main(void)
{
	if (UINTPTR_MAX > 0xffffffffu) {
		printf("pointers are 64-bit, so no buffer is longer than "
		       "PTRDIFF_MAX: skipped\n");
		return 77;
	}
	uint8_t *region =
		mmap(NULL, REGION_BYTES, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (region == MAP_FAILED) {
		printf("cannot map %lu bytes: skipped\n",
		       (unsigned long)REGION_BYTES);
		return 77;
	}
	printf("region of %lu bytes at %p (PTRDIFF_MAX %ld)\n",
	       (unsigned long)REGION_BYTES, (void *)region, (long)PTRDIFF_MAX);
	check_stream(region, 300000, 1);
	check_stream(region, 300000, 9);
	munmap(region, REGION_BYTES);
	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("all agree\n");
	return 0;
}
#endif
