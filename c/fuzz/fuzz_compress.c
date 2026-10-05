/*
 * fuzz_compress.c - libFuzzer target: bz2-ng's encoder against libbz2's
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * The input is a header of ENC_HDR bytes that chooses the level, workFactor
 * and call sequence, followed by the data.  Stock libbz2 1.0.8
 * (test/reference, symbols renamed ref_*) and bz2-ng compress the data with
 * identical BZ2_bzCompress calls: BZ_RUN, BZ_FLUSH and BZ_FINISH with input
 * and output in random pieces down to zero bytes, and now and then an invalid
 * action or a flush or finish whose action or avail_in the caller changes
 * (BZ_SEQUENCE_ERROR) or a BZ_RUN that makes no progress (BZ_PARAM_ERROR).
 * After every call the return code, next_in, avail_in, next_out, avail_out,
 * total_in and total_out must agree, and neither library may change a byte
 * past next_out; the bytes delivered are compared once the stream is
 * finished, where an allowed difference can be recognised (below).  Each
 * call's input is copied into a heap block of exactly
 * avail_in bytes, so that AddressSanitizer reports any read outside
 * [next_in, next_in + avail_in) (docs/spec.md section 2.2).
 *
 * At the end the streams must be identical except where docs/spec.md
 * section 2.3 allows: in the origPtr of a block that is exactly periodic,
 * where bz2-ng writes the last of the tied rows.  So every differing bit must
 * lie in a block's origPtr field, the streams must be identical with
 * libbz2's values put back, bz2-ng's stream must decode to the data (which
 * a different row can only do when its rotation equals the block, so the
 * block is periodic), and bz2-ng's value must be the last tied row: with the
 * field one higher the stream must no longer decode to the data.  Then
 * bz2-ng's stream is decoded by both libraries.
 *
 * Header (missing bytes read as zero):
 *   byte 0      bits 0-3: level, 1 + value % 9; bit 4: start the 64-bit
 *               totals just below 2^32 (callers may write them); bit 5:
 *               the plain schedule instead, BZ_FINISH with all the data and
 *               byte 3's output space until the stream ends
 *   byte 1      workFactor (above 250 is a parameter error)
 *   bytes 2, 3  largest input and output piece (pick_max)
 *   byte 4      chance per mille that a BZ_RUN call is a BZ_FLUSH
 *   byte 5      bits 0-5: chance per mille of an injected sequence error;
 *               bits 6-7: chance in 30 per mille of no output space
 *   byte 6      chance per mille that BZ_FINISH leaves input unused
 *   bytes 7-10  schedule seed
 *
 * Blocks end at a BZ_FLUSH or BZ_FINISH: inputs short enough to fuzz quickly
 * never fill one.  The block-full boundary is test/enc_difftest.c's capacity
 * suite's.
 */

#include <limits.h>

#include "fuzz_common.h"
#include "reference_api.h"

#define ENC_HDR 11
/* Bytes past the offered output space compared after every call. */
#define OUT_WINDOW 256
/* Calls per input after which the run stops (keeps each input fast). */
#define MAX_CALLS 100000

typedef struct {
	int level, work_factor, preset_totals, plain;
	size_t in_max, out_max;
	int p_flush, p_err, p_zero_out, p_finish_partial;
	uint64_t seed;
} EncConfig;

/* The largest piece for a header byte: fine steps up to 128, then coarse. */
static size_t
pick_max(uint8_t v)
{
	return v < 128 ? 1 + (size_t)v : 129 + (size_t)(v - 128) * 512;
}

/* Reads the header h[0..ENC_HDR). */
static EncConfig
parse_header(const uint8_t *h)
{
	EncConfig c;

	c.level = 1 + (h[0] & 15) % 9;
	c.preset_totals = (h[0] >> 4) & 1;
	c.plain = (h[0] >> 5) & 1;
	c.work_factor = h[1];
	c.in_max = pick_max(h[2]);
	c.out_max = pick_max(h[3]);
	c.p_flush = h[4];
	c.p_err = h[5] & 63;
	c.p_zero_out = (h[5] >> 6) * 30;
	c.p_finish_partial = h[6];
	c.seed = ((uint64_t)h[7] | (uint64_t)h[8] << 8 | (uint64_t)h[9] << 16 |
		  (uint64_t)h[10] << 24) ^
		 0x9E3779B97F4A7C15ull;
	return c;
}

/* A value below n (0 if n is 0). */
static size_t
rnd_below(uint64_t *st, size_t n)
{
	return n ? (size_t)(fuzz_rnd(st) % n) : 0;
}

/* A piece size: zero, one, small, or up to max. */
static size_t
pick_size(uint64_t *st, size_t max)
{
	switch (rnd_below(st, 8)) {
	case 0:
		return 0;
	case 1:
		return 1;
	case 2:
		return 1 + rnd_below(st, 8);
	case 3:
		return 1 + rnd_below(st, 64);
	default:
		return 1 + rnd_below(st, max);
	}
}

/* One library's side of the run: its stream and its growing output. */
typedef struct {
	bz_stream s;
	uint8_t *out;
	size_t cap;
} Side;

/*
 * Grows both outputs, and the buffer for the pattern placed in them, to at
 * least need bytes.
 */
static void
reserve(Side *a, Side *b, uint8_t **pattern, size_t need)
{
	if (a->cap >= need)
		return;
	size_t cap = a->cap ? a->cap : 4096;
	while (cap < need)
		cap *= 2;
	a->out = realloc(a->out, cap);
	b->out = realloc(b->out, cap);
	*pattern = realloc(*pattern, cap);
	a->cap = b->cap = cap;
}

/*
 * Fails if either library changed a byte of [from, to), which held
 * pattern[from..to) before the call `what`.
 */
static void
check_untouched(const char *what, const Side *a, const Side *b,
		const uint8_t *pattern, size_t from, size_t to)
{
	const size_t ka = fuzz_first_difference(a->out, pattern, from, to);
	const size_t kb = fuzz_first_difference(b->out, pattern, from, to);
	if (ka < to || kb < to)
		fuzz_fail("compress %s: buffer byte %zu past next_out (at %zu) "
			  "changed by %s",
			  what, ka < kb ? ka : kb, from,
			  ka <= kb ? "libbz2" : "bz2-ng");
}

/* Compares what one call did to the two streams; fails on any difference. */
static void
compare_call(const char *what, int ra, int rb, const Side *a, const Side *b,
	     const uint8_t *in)
{
	const long long field[7][2] = {
		{ra, rb},
		{(long long)((const uint8_t *)a->s.next_in - in),
		 (long long)((const uint8_t *)b->s.next_in - in)},
		{(long long)a->s.avail_in, (long long)b->s.avail_in},
		{(long long)((const uint8_t *)a->s.next_out - a->out),
		 (long long)((const uint8_t *)b->s.next_out - b->out)},
		{(long long)a->s.avail_out, (long long)b->s.avail_out},
		{(long long)fuzz_total_in(&a->s),
		 (long long)fuzz_total_in(&b->s)},
		{(long long)fuzz_total_out(&a->s),
		 (long long)fuzz_total_out(&b->s)},
	};
	static const char *const name[7] = {
		"return code", "next_in",  "avail_in", "next_out offset",
		"avail_out",   "total_in", "total_out"};

	for (int f = 0; f < 7; f++)
		if (field[f][0] != field[f][1])
			fuzz_fail("compress %s: %s libbz2 %lld bz2-ng %lld",
				  what, name[f], field[f][0], field[f][1]);
}

/* n <= 32 bits of p from bit position bit, most significant first. */
static uint32_t
get_bits(const uint8_t *p, uint64_t bit, int n)
{
	uint32_t v = 0;
	for (int i = 0; i < n; i++, bit++)
		v = v << 1 | ((p[bit >> 3] >> (7 - (bit & 7))) & 1);
	return v;
}

/* Writes the n low bits of v to p at bit position bit. */
static void
set_bits(uint8_t *p, uint64_t bit, int n, uint32_t v)
{
	for (int i = n - 1; i >= 0; i--, bit++) {
		const uint8_t m = (uint8_t)(0x80 >> (bit & 7));
		if ((v >> i) & 1)
			p[bit >> 3] |= m;
		else
			p[bit >> 3] &= (uint8_t)~m;
	}
}

/*
 * The first bit of the 24-bit origPtr field that contains bit `bit` in both
 * streams: a field preceded, in both, by the 48-bit block magic, the 32-bit
 * block CRC and the randomised bit.  UINT64_MAX if there is none.
 */
static uint64_t
origptr_field(const uint8_t *sa, const uint8_t *sb, uint64_t nbits,
	      uint64_t bit)
{
	for (uint64_t f = bit >= 23 ? bit - 23 : 0; f <= bit; f++) {
		if (f < 81 || f + 24 > nbits)
			continue;
		if (get_bits(sa, f - 81, 24) == 0x314159 &&
		    get_bits(sa, f - 57, 24) == 0x265359 &&
		    get_bits(sb, f - 81, 24) == 0x314159 &&
		    get_bits(sb, f - 57, 24) == 0x265359)
			return f;
	}
	return UINT64_MAX;
}

/*
 * Whether decoder `which` (0 libbz2, 1 bz2-ng) decodes the stream
 * s[0..len) to exactly data[0..n).
 */
static int
decodes_to(int which, const uint8_t *s, size_t len, const uint8_t *data,
	   size_t n)
{
	unsigned dlen = (unsigned)n + 1;
	uint8_t *dec = malloc(n + 1);
	const int r = (which ? BZ2_bzBuffToBuffDecompress :
			       ref_BZ2_bzBuffToBuffDecompress)(
		(char *)dec, &dlen, (char *)s, (unsigned)len, 0, 0);
	const int ok = r == BZ_OK && dlen == n && memcmp(dec, data, n) == 0;
	free(dec);
	return ok;
}

/*
 * Checks the two finished streams sa (libbz2) and sb (bz2-ng), each len
 * bytes, for the data in[0..n) (see the file comment).
 */
static void
check_streams(const uint8_t *sa, const uint8_t *sb, size_t len,
	      const uint8_t *in, size_t n)
{
	if (memcmp(sa, sb, len) != 0) {
		const uint64_t nbits = (uint64_t)len * 8;
		uint8_t *patched = malloc(len);
		uint8_t *higher = malloc(len);
		memcpy(patched, sb, len);
		for (size_t byte = 0; byte < len; byte++) {
			if (sa[byte] == patched[byte])
				continue;
			for (uint64_t bit = (uint64_t)byte * 8;
			     bit < (uint64_t)byte * 8 + 8; bit++) {
				if (get_bits(sa, bit, 1) ==
				    get_bits(patched, bit, 1))
					continue;
				const uint64_t f =
					origptr_field(sa, patched, nbits, bit);
				if (f == UINT64_MAX)
					fuzz_fail("streams of %zu bytes differ "
						  "at bit %llu, outside an "
						  "origPtr field",
						  len, (unsigned long long)bit);
				const uint32_t ref = get_bits(sa, f, 24);
				const uint32_t ng = get_bits(patched, f, 24);
				memcpy(higher, sb, len);
				set_bits(higher, f, 24, ng + 1);
				if (ng < ref ||
				    decodes_to(0, higher, len, in, n))
					fuzz_fail("origPtr at bit %llu: bz2-ng "
						  "%u is not the last tied row "
						  "(libbz2 %u)",
						  (unsigned long long)f, ng,
						  ref);
				set_bits(patched, f, 24, ref);
			}
		}
		if (memcmp(sa, patched, len) != 0)
			fuzz_fail("streams differ beyond origPtr");
		free(patched);
		free(higher);
	}
	if (!decodes_to(0, sb, len, in, n))
		fuzz_fail("libbz2 does not decode bz2-ng's stream to the data");
	if (!decodes_to(1, sb, len, in, n))
		fuzz_fail("bz2-ng does not decode its own stream to the data");
}

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* libFuzzer's set-up hook: checks the kernel tier. */
int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc;
	(void)argv;
	fuzz_init_tier("fuzz_compress");
	return 0;
}

/* Compresses one input with both libraries (see the file comment). */
int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	uint8_t hdr[ENC_HDR] = {0};
	memcpy(hdr, data, size < ENC_HDR ? size : ENC_HDR);
	const EncConfig c = parse_header(hdr);
	const size_t n = size > ENC_HDR ? size - ENC_HDR : 0;
	uint8_t *in = malloc(n);
	memcpy(in, data + (size - n), n);

	Side a = {.out = NULL}, b = {.out = NULL};
	uint8_t *pattern = NULL;
	memset(&a.s, 0, sizeof a.s);
	memset(&b.s, 0, sizeof b.s);
	int ra = ref_BZ2_bzCompressInit(&a.s, c.level, 0, c.work_factor);
	int rb = BZ2_bzCompressInit(&b.s, c.level, 0, c.work_factor);
	if (ra != rb)
		fuzz_fail("compress init level %d workFactor %d: libbz2 %d "
			  "bz2-ng %d",
			  c.level, c.work_factor, ra, rb);
	if (ra != BZ_OK) {
		free(in);
		return 0;
	}
	uint64_t st = c.seed | 1;
	if (c.preset_totals) {
		a.s.total_in_lo32 = b.s.total_in_lo32 =
			0xffffffffu - (unsigned)rnd_below(&st, n + 1);
		a.s.total_out_lo32 = b.s.total_out_lo32 =
			0xffffffffu - (unsigned)rnd_below(&st, n / 8 + 64);
	}

	size_t pos = 0, out_pos = 0, cur_avail = 0;
	int phase = BZ_RUN, done = 0, no_progress = 0;
	long call;
	for (call = 0; !done && call < MAX_CALLS; call++) {
		int action, inject = 0;
		size_t avail, avail_out;
		if (c.plain) {
			action = BZ_FINISH;
			avail = phase == BZ_FINISH ? cur_avail : n - pos;
		} else if (phase == BZ_RUN) {
			const size_t remaining = n - pos;
			const size_t r = rnd_below(&st, 1000);
			if (remaining == 0 || r < 3) {
				action = BZ_FINISH;
				avail = (int)rnd_below(&st, 1000) <
							c.p_finish_partial ?
						rnd_below(&st, remaining + 1) :
						remaining;
			} else {
				action = (int)r < 3 + c.p_flush ? BZ_FLUSH :
								  BZ_RUN;
				avail = pick_size(&st, c.in_max);
				if (avail > remaining)
					avail = remaining;
				if (action == BZ_RUN &&
				    rnd_below(&st, 1000) < 5)
					action = 7; /* not an action */
			}
		} else {
			/* a flush or finish in progress, with the same input */
			action = phase;
			avail = cur_avail;
			if ((int)rnd_below(&st, 1000) < c.p_err) {
				inject = 1;
				switch (rnd_below(&st, 3)) {
				case 0:
					action = phase == BZ_FLUSH ? BZ_FINISH :
								     BZ_FLUSH;
					break;
				case 1:
					action = BZ_RUN;
					break;
				default:
					if (avail > 0 && rnd_below(&st, 2))
						avail--;
					else if (pos + avail < n)
						avail++;
					else
						action = BZ_RUN;
				}
			}
		}
		if (c.plain)
			avail_out = c.out_max;
		else if (no_progress >= 2)
			avail_out = 1 + rnd_below(&st, c.out_max);
		else
			avail_out = (int)rnd_below(&st, 1000) < c.p_zero_out ?
					    0 :
					    pick_size(&st, c.out_max);

		/* a pattern new to this call where the libraries write */
		const size_t fill_end = out_pos + avail_out + OUT_WINDOW;
		reserve(&a, &b, &pattern, fill_end);
		fuzz_fill_pattern(pattern + out_pos, a.out + out_pos,
				  fill_end - out_pos, c.seed + (uint64_t)call);
		memcpy(b.out + out_pos, pattern + out_pos, fill_end - out_pos);
		uint8_t *piece = malloc(avail);
		memcpy(piece, in + pos, avail);
		a.s.next_in = b.s.next_in = (char *)piece;
		a.s.avail_in = b.s.avail_in = (unsigned)avail;
		a.s.next_out = (char *)a.out + out_pos;
		b.s.next_out = (char *)b.out + out_pos;
		a.s.avail_out = b.s.avail_out = (unsigned)avail_out;
		ra = ref_BZ2_bzCompress(&a.s, action);
		rb = BZ2_bzCompress(&b.s, action);
		char what[160];
		snprintf(what, sizeof what,
			 "level %d call %ld action %d avail_in %zu avail_out "
			 "%zu%s",
			 c.level, call, action, avail, avail_out,
			 inject ? " (injected)" : "");
		compare_call(what, ra, rb, &a, &b, piece);
		const size_t used = (size_t)((uint8_t *)a.s.next_in - piece);
		free(piece);
		const size_t new_out =
			(size_t)((uint8_t *)a.s.next_out - a.out);
		check_untouched(what, &a, &b, pattern, new_out, fill_end);
		no_progress =
			used == 0 && new_out == out_pos ? no_progress + 1 : 0;
		pos += used;
		out_pos = new_out;
		/*
		 * An injected error leaves both libraries as they were; the
		 * caller goes on with the avail_in it had.
		 */
		if (inject)
			continue;
		cur_avail = a.s.avail_in;
		/* FLUSH and FINISH switch the mode before anything can fail */
		if (phase == BZ_RUN &&
		    (action == BZ_FLUSH || action == BZ_FINISH))
			phase = action;
		if (phase == BZ_FLUSH && ra == BZ_RUN_OK)
			phase = BZ_RUN;
		if (ra == BZ_STREAM_END)
			done = 1;
	}

	if (done) {
		/* a call after the end is a sequence error in both */
		const int action = (int)rnd_below(&st, 3);
		const size_t fill_end = out_pos + 16 + OUT_WINDOW;
		reserve(&a, &b, &pattern, fill_end);
		fuzz_fill_pattern(pattern + out_pos, a.out + out_pos,
				  fill_end - out_pos, c.seed ^ 0xE4D);
		memcpy(b.out + out_pos, pattern + out_pos, fill_end - out_pos);
		a.s.next_in = b.s.next_in = (char *)in + pos;
		a.s.next_out = (char *)a.out + out_pos;
		b.s.next_out = (char *)b.out + out_pos;
		a.s.avail_out = b.s.avail_out = 16;
		ra = ref_BZ2_bzCompress(&a.s, action);
		rb = BZ2_bzCompress(&b.s, action);
		compare_call("after BZ_STREAM_END", ra, rb, &a, &b, in + pos);
		check_untouched("after BZ_STREAM_END", &a, &b, pattern, out_pos,
				fill_end);
		check_streams(a.out, b.out, out_pos, in, pos);
	}
	ra = ref_BZ2_bzCompressEnd(&a.s);
	rb = BZ2_bzCompressEnd(&b.s);
	if (ra != rb || b.s.state != NULL)
		fuzz_fail("compress end: libbz2 %d bz2-ng %d", ra, rb);
	free(a.out);
	free(b.out);
	free(pattern);
	free(in);
	return 0;
}
