/*
 * fuzz_decompress.c - libFuzzer target: bz2-ng's decoder against libbz2's
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * The input is a header of DEC_HDR bytes that chooses how the stream is
 * decoded, followed by the stream.  Stock libbz2 1.0.8 (test/reference,
 * symbols renamed ref_*) and bz2-ng decode it with identical calls; after
 * every call the return code, next_in, avail_in, next_out, avail_out,
 * total_in and total_out, the delivered bytes and the bytes just past
 * next_out must agree.  Each call's input is copied into a heap block of
 * exactly avail_in bytes, so that AddressSanitizer reports any read outside
 * [next_in, next_in + avail_in) (docs/spec.md section 2.2).
 *
 * Header (missing bytes read as zero):
 *   byte 0     bit 0: small; bit 1: memory layout (0 in-formula, 1 wide);
 *              bits 2-4: schedule (enum dec_schedule); bits 5-6: n, where
 *              the n-th bzalloc call and every later one fail (0: none);
 *              bit 7: start the 64-bit totals just below 2^32 (callers may
 *              write them after BZ2_bzDecompressInit)
 *   byte 1     input chunk parameter
 *   byte 2     output chunk parameter
 *   byte 3     output space: below 0x80 DEC_BIG_OUT bytes, otherwise
 *              (byte 3 - 0x80) * 64
 *   bytes 4-7  schedule seed
 *
 * After the first return other than BZ_OK libbz2 is not called again: its
 * manual allows only BZ2_bzDecompressEnd then, and resuming reaches undefined
 * behaviour inside libbz2 itself (research/M1e-decoder-api-test.md).  bz2-ng
 * is called alone twice more and held to docs/spec.md section 2.5: once the
 * stream's trailer has been read (BZ_STREAM_END, or a combined-CRC mismatch,
 * which is when libbz2 is idle) it returns BZ_SEQUENCE_ERROR, otherwise the
 * first error again, and it changes no stream field and writes no byte.
 */

#include "dec.h"
#include "fuzz_common.h"
#include "reference_api.h"
#include "ref_state.h"

#define DEC_HDR 8
/* Output space when the header does not ask for a small one. */
#define DEC_BIG_OUT (1u << 20)
/*
 * Bytes past the largest output space in each buffer, so that a write past
 * the offered space lands in memory the target owns and is compared.
 */
#define OUT_SLACK 512
/*
 * Bytes from next_out that get a fresh pattern before every call and are
 * compared after it; the bulk un-RLE1 kernel's stores reach at most 15 bytes
 * past what it delivers.
 */
#define OUT_WINDOW 256
/* Calls per input after which the run stops (keeps each input fast). */
#define MAX_CALLS 100000

/* How the input and output are offered, call by call. */
enum dec_schedule {
	SCH_WHOLE,     /* everything at once */
	SCH_FIXED,     /* 1 + byte 1 in, 1 + byte 2 out */
	SCH_IN_BYTES,  /* 1-4 bytes in, all the output space */
	SCH_OUT_BYTES, /* all the input, 1-8 bytes out */
	SCH_SPLIT_IN,  /* the first call gets bytes 1-2 of input, then all */
	SCH_SPLIT_OUT, /* the first call gets bytes 1-2 of output, then all */
	SCH_RANDOM,    /* random sizes, some zero, maxima from bytes 1 and 2 */
	SCH_TINY,      /* random sizes from 0 to 3 */
};

static const char *const schedule_name[8] = {
	"whole",    "fixed",	 "in-bytes", "out-bytes",
	"split-in", "split-out", "random",   "tiny"};

typedef struct {
	int small, layout, fail_at, preset_totals;
	enum dec_schedule sched;
	unsigned in_param, out_param;
	size_t out_cap;
	uint64_t seed;
} DecConfig;

/* Reads the header h[0..DEC_HDR). */
static DecConfig
parse_header(const uint8_t *h)
{
	DecConfig c;

	c.small = h[0] & 1;
	c.layout = (h[0] >> 1) & 1 ? BZNG_LAYOUT_WIDE : BZNG_LAYOUT_FIT;
	c.sched = (enum dec_schedule)((h[0] >> 2) & 7);
	c.fail_at = (h[0] >> 5) & 3;
	c.preset_totals = h[0] >> 7;
	c.in_param = h[1];
	c.out_param = h[2];
	c.out_cap = h[3] < 0x80 ? DEC_BIG_OUT : (size_t)(h[3] - 0x80) * 64;
	c.seed = (uint64_t)h[4] | (uint64_t)h[5] << 8 | (uint64_t)h[6] << 16 |
		 (uint64_t)h[7] << 24;
	return c;
}

/* A random chunk size up to max: mostly small, sometimes zero. */
static size_t
random_chunk(uint64_t *st, size_t max)
{
	const uint64_t r = fuzz_rnd(st);

	switch (r & 7) {
	case 0:
		return 0;
	case 1:
	case 2:
		return 1 + (r >> 8) % 4;
	case 3:
		return 1 + (r >> 8) % 64;
	default:
		return (r >> 8) % (max + 1);
	}
}

/*
 * The input and output sizes for call number `call` (unclipped; SIZE_MAX for
 * everything left).
 */
static void
next_chunk(const DecConfig *c, long call, uint64_t *st, size_t *ci, size_t *co)
{
	const size_t split = (size_t)c->in_param << 8 | c->out_param;

	*ci = SIZE_MAX;
	*co = SIZE_MAX;
	switch (c->sched) {
	case SCH_WHOLE:
		break;
	case SCH_FIXED:
		*ci = 1 + (size_t)c->in_param;
		*co = 1 + (size_t)c->out_param;
		break;
	case SCH_IN_BYTES:
		*ci = 1 + c->in_param % 4;
		break;
	case SCH_OUT_BYTES:
		*co = 1 + c->out_param % 8;
		break;
	case SCH_SPLIT_IN:
		if (call == 0)
			*ci = split;
		break;
	case SCH_SPLIT_OUT:
		if (call == 0)
			*co = split;
		break;
	case SCH_RANDOM:
		*ci = random_chunk(st, 16 * (size_t)c->in_param + 1);
		*co = random_chunk(st, 256 * (size_t)c->out_param + 1);
		break;
	case SCH_TINY:
		*ci = fuzz_rnd(st) & 3;
		*co = fuzz_rnd(st) & 3;
		break;
	}
}

/* Compares what one call did to the two streams; fails on any difference. */
static void
compare_call(const DecConfig *c, long call, int ra, int rb, const bz_stream *a,
	     const uint8_t *in_a, const uint8_t *out_a, const bz_stream *b,
	     const uint8_t *in_b, const uint8_t *out_b)
{
	const long long field[7][2] = {
		{ra, rb},
		{(long long)((const uint8_t *)a->next_in - in_a),
		 (long long)((const uint8_t *)b->next_in - in_b)},
		{(long long)a->avail_in, (long long)b->avail_in},
		{(long long)((const uint8_t *)a->next_out - out_a),
		 (long long)((const uint8_t *)b->next_out - out_b)},
		{(long long)a->avail_out, (long long)b->avail_out},
		{(long long)fuzz_total_in(a), (long long)fuzz_total_in(b)},
		{(long long)fuzz_total_out(a), (long long)fuzz_total_out(b)},
	};
	static const char *const name[7] = {
		"return code", "next_in",  "avail_in", "next_out offset",
		"avail_out",   "total_in", "total_out"};

	for (int f = 0; f < 7; f++)
		if (field[f][0] != field[f][1])
			fuzz_fail("decode small=%d layout=%d schedule=%s call "
				  "%ld: %s libbz2 %lld bz2-ng %lld",
				  c->small, c->layout, schedule_name[c->sched],
				  call, name[f], field[f][0], field[f][1]);
}

/*
 * Calls bz2-ng alone after its first return `first` other than BZ_OK, with
 * input and output it could use if it went on, and checks docs/spec.md
 * section 2.5: `expected` back, every field as set, nothing written.
 */
static void
call_after_end(const DecConfig *c, bz_stream *b, uint8_t *out_b, size_t out_off,
	       const uint8_t *stream, size_t len, size_t in_off, int first,
	       int expected)
{
	/* the rest of the stream, or all of it if none is left */
	const size_t from = in_off < len ? in_off : 0;
	uint8_t *in = malloc(len - from);
	uint8_t pattern[OUT_WINDOW], scratch[OUT_WINDOW];

	memcpy(in, stream + from, len - from);
	for (int k = 0; k < 2; k++) {
		b->next_in = (char *)in;
		b->avail_in = (unsigned)(len - from);
		b->next_out = (char *)out_b + out_off;
		b->avail_out = OUT_WINDOW;
		fuzz_fill_pattern(out_b + out_off, pattern, OUT_WINDOW,
				  0xAF7E8 + (uint64_t)k);
		memcpy(scratch, pattern, OUT_WINDOW);
		const bz_stream before = *b;
		const int rb = BZ2_bzDecompress(b);
		if (rb != expected)
			fuzz_fail("decode small=%d layout=%d schedule=%s: call "
				  "%d after %d returned %d, expected %d",
				  c->small, c->layout, schedule_name[c->sched],
				  k, first, rb, expected);
		if (b->next_in != before.next_in ||
		    b->avail_in != before.avail_in ||
		    b->next_out != before.next_out ||
		    b->avail_out != before.avail_out ||
		    fuzz_total_in(b) != fuzz_total_in(&before) ||
		    fuzz_total_out(b) != fuzz_total_out(&before))
			fuzz_fail("decode small=%d layout=%d schedule=%s: call "
				  "%d after %d changed a stream field",
				  c->small, c->layout, schedule_name[c->sched],
				  k, first);
		const size_t d = fuzz_first_difference(scratch, out_b + out_off,
						       0, OUT_WINDOW);
		if (d < OUT_WINDOW)
			fuzz_fail("decode small=%d layout=%d schedule=%s: call "
				  "%d after %d wrote output byte %zu",
				  c->small, c->layout, schedule_name[c->sched],
				  k, first, d);
	}
	free(in);
}

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* libFuzzer's set-up hook: checks the kernel tier. */
int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc;
	(void)argv;
	fuzz_init_tier("fuzz_decompress");
	return 0;
}

/* Decodes one input with both libraries (see the file comment). */
int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	uint8_t hdr[DEC_HDR] = {0};
	memcpy(hdr, data, size < DEC_HDR ? size : DEC_HDR);
	const DecConfig c = parse_header(hdr);
	const uint8_t *stream = size > DEC_HDR ? data + DEC_HDR : data + size;
	const size_t len = size > DEC_HDR ? size - DEC_HDR : 0;

	/*
	 * Both buffers start equal; the bytes a call may write get a fresh
	 * pattern before it (below), and the whole buffers are compared at the
	 * end.
	 */
	const size_t buf_len = c.out_cap + OUT_SLACK;
	uint8_t *out_a = malloc(buf_len), *out_b = malloc(buf_len);
	memset(out_a, 0xA5, buf_len);
	memset(out_b, 0xA5, buf_len);

	FuzzAlloc aa = {0, c.fail_at, 0}, ab = {0, c.fail_at, 0};
	bz_stream a, b;
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	a.bzalloc = b.bzalloc = fuzz_alloc;
	a.bzfree = b.bzfree = fuzz_free;
	a.opaque = &aa;
	b.opaque = &ab;
	int ra = ref_BZ2_bzDecompressInit(&a, 0, c.small);
	int rb = bzng_decompress_init(&b, 0, c.small, c.layout);
	if (ra != rb)
		fuzz_fail("decompress init: libbz2 %d bz2-ng %d", ra, rb);
	if (ra != BZ_OK) {
		free(out_a);
		free(out_b);
		return 0;
	}
	if (c.preset_totals) {
		a.total_in_lo32 = b.total_in_lo32 =
			0xffffffffu - (unsigned)(c.seed % (len + 1));
		a.total_out_lo32 = b.total_out_lo32 =
			0xffffffffu - (unsigned)(c.seed >> 16);
	}

	uint64_t st = c.seed | 1;
	size_t in_off = 0, out_off = 0;
	int ret = BZ_OK, stalls = 0;
	long call;
	for (call = 0; call < MAX_CALLS; call++) {
		size_t ci, co;
		next_chunk(&c, call, &st, &ci, &co);
		if (ci > len - in_off)
			ci = len - in_off;
		if (co > c.out_cap - out_off)
			co = c.out_cap - out_off;
		uint8_t *in = malloc(ci);
		memcpy(in, stream + in_off, ci);
		a.next_in = b.next_in = (char *)in;
		a.avail_in = b.avail_in = (unsigned)ci;
		a.next_out = (char *)out_a + out_off;
		b.next_out = (char *)out_b + out_off;
		a.avail_out = b.avail_out = (unsigned)co;
		/* a pattern new to this call where the decoders write next */
		const size_t fresh = buf_len - out_off < OUT_WINDOW ?
					     buf_len - out_off :
					     OUT_WINDOW;
		fuzz_fill_pattern(out_a + out_off, out_b + out_off, fresh,
				  c.seed + (uint64_t)call + 1);
		ra = ref_BZ2_bzDecompress(&a);
		rb = BZ2_bzDecompress(&b);
		compare_call(&c, call, ra, rb, &a, in, out_a, &b, in, out_b);
		const size_t used = (size_t)((uint8_t *)a.next_in - in);
		const size_t oa = (size_t)((uint8_t *)a.next_out - out_a);
		free(in);
		/* the delivered bytes and the bytes just past next_out */
		const size_t end =
			oa + OUT_WINDOW < buf_len ? oa + OUT_WINDOW : buf_len;
		const size_t k =
			fuzz_first_difference(out_a, out_b, out_off, end);
		if (k < end)
			fuzz_fail("decode small=%d layout=%d schedule=%s call "
				  "%ld: %s %zu (next_out at %zu)",
				  c.small, c.layout, schedule_name[c.sched],
				  call,
				  k < oa ? "output byte" :
					   "buffer byte past next_out",
				  k, oa);
		const int progress = used != 0 || oa != out_off;
		in_off += used;
		out_off = oa;
		ret = ra;
		if (ra != BZ_OK)
			break;
		if (!progress && (in_off == len || out_off == c.out_cap ||
				  ci == 0 || co == 0)) {
			if (++stalls >= 2)
				break;
		} else {
			stalls = 0;
		}
	}
	const size_t k = fuzz_first_difference(out_a, out_b, 0, buf_len);
	if (k < buf_len)
		fuzz_fail("decode small=%d layout=%d schedule=%s: buffer byte "
			  "%zu differs at the end (next_out at %zu)",
			  c.small, c.layout, schedule_name[c.sched], k,
			  out_off);

	if (ret != BZ_OK) {
		/*
		 * libbz2 is idle after its trailer, whether the combined CRC
		 * matched (BZ_STREAM_END) or not (BZ_DATA_ERROR)
		 */
		const int idle = ref_decompress_idle(&a);
		if (ret == BZ_STREAM_END && !idle)
			fuzz_fail("libbz2 not idle after BZ_STREAM_END");
		call_after_end(&c, &b, out_b, out_off, stream, len, in_off, ret,
			       idle ? BZ_SEQUENCE_ERROR : ret);
	}

	ra = ref_BZ2_bzDecompressEnd(&a);
	rb = BZ2_bzDecompressEnd(&b);
	if (ra != rb)
		fuzz_fail("decompress end: libbz2 %d bz2-ng %d", ra, rb);
	if (aa.in_use != 0 || ab.in_use != 0)
		fuzz_fail("decompress leak: libbz2 %zu bytes, bz2-ng %zu",
			  aa.in_use, ab.in_use);
	if (aa.calls != ab.calls)
		fuzz_fail("bzalloc calls: libbz2 %ld bz2-ng %ld", aa.calls,
			  ab.calls);
	free(out_a);
	free(out_b);
	return 0;
}
