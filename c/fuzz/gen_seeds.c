/*
 * gen_seeds.c - writes the fuzz targets' seed corpora
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * usage: fuzz_seeds DIR
 *
 * Writes a seed corpus for each fuzz target:
 *   DIR/decompress/  valid streams from stock libbz2 at several levels and
 *                    sizes (some with randomised blocks), a stream followed
 *                    by trailing bytes, and the hand-built edge cases of the
 *                    decoder tester (test/dec_difftest.c, gen_crafted), each
 *                    behind a few headers that choose the call schedule; and
 *                    the tester's block endings for the bulk un-RLE1
 *                    kernel's stores (its slack phase), each behind one
 *                    header with small output pieces (fuzz/fuzz_decompress.c)
 *   DIR/compress/    small inputs around the RLE1 unit lengths, text,
 *                    random, periodic and constant data, behind headers for
 *                    the plain and random schedules at several levels
 *                    (fuzz/fuzz_compress.c)
 *   DIR/sort/        the sorter's test families in small sizes: short and
 *                    periodic words, Fibonacci, Thue-Morse and de Bruijn
 *                    words, random blocks over small and full alphabets,
 *                    runs and zigzags (fuzz/fuzz_sort.c)
 * Every input is kept small, because the fuzzer's speed falls with input
 * size: repetitive data gives full-size blocks in a few kilobytes.
 *
 * The decoder tester's generators are reached by compiling its source here
 * with its main renamed, so that the seeds follow the tester as it grows.
 */
#define main dec_difftest_main
#include "dec_difftest.c"
#undef main

#include <errno.h>
#include <sys/stat.h>

/* Largest stream written as a seed (the campaign's -max_len allows it). */
#define SEED_MAX_STREAM (120u * 1024)

static const char *seed_dir;
static unsigned long seeds_written;

/* Creates directory path if it does not exist; exits on failure. */
static void
seed_mkdir(const char *path)
{
	if (mkdir(path, 0777) != 0 && errno != EEXIST) {
		perror(path);
		exit(2);
	}
}

/*
 * Writes head[0..head_len) followed by body[0..body_len) as the next seed of
 * target (a subdirectory of seed_dir).
 */
static void
seed_write(const char *target, const uint8_t *head, size_t head_len,
	   const uint8_t *body, size_t body_len)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/%s/seed-%05lu", seed_dir, target,
		 seeds_written++);
	FILE *f = fopen(path, "wb");
	if (f == NULL || fwrite(head, 1, head_len, f) != head_len ||
	    fwrite(body, 1, body_len, f) != body_len || fclose(f) != 0) {
		perror(path);
		exit(2);
	}
}

/*
 * The decoder header (fuzz/fuzz_decompress.c) for small, layout (0 or 1),
 * schedule, the two chunk parameters and the output byte.
 */
static void
seed_dec_header(uint8_t h[8], int small, int layout, int schedule,
		uint8_t in_param, uint8_t out_param, uint8_t out_byte)
{
	h[0] = (uint8_t)(small | layout << 1 | schedule << 2);
	h[1] = in_param;
	h[2] = out_param;
	h[3] = out_byte;
	h[4] = 0x5E;
	h[5] = 0xED;
	h[6] = (uint8_t)seeds_written;
	h[7] = (uint8_t)(seeds_written >> 8);
}

/*
 * Writes the stream comp[0..len) behind several headers: whole buffers on
 * both layouts, small mode with a random schedule and the totals started
 * just below 2^32, and for short streams
 * byte-wise input, small output pieces and tiny random pieces into 1,024
 * bytes of output space.
 */
static void
seed_dec_stream(const uint8_t *comp, size_t len)
{
	uint8_t h[8];

	if (len > SEED_MAX_STREAM)
		return;
	seed_dec_header(h, 0, 1, 0, 0, 0, 0);
	seed_write("decompress", h, 8, comp, len);
	seed_dec_header(h, 0, 0, 0, 0, 0, 0);
	seed_write("decompress", h, 8, comp, len);
	seed_dec_header(h, 1, 1, 6, 0x40, 0x10, 0);
	h[0] |= 0x80;
	seed_write("decompress", h, 8, comp, len);
	if (len <= 4096) {
		seed_dec_header(h, 0, 1, 2, 0, 0, 0);
		seed_write("decompress", h, 8, comp, len);
		seed_dec_header(h, 1, 0, 3, 0, 5, 0);
		seed_write("decompress", h, 8, comp, len);
		seed_dec_header(h, 0, 1, 7, 0, 0, 0x90);
		seed_write("decompress", h, 8, comp, len);
	}
}

/* Text from a small dictionary into b[0..n). */
static void
seed_text(uint8_t *b, size_t n)
{
	static const char *const words[] = {
		"the ",	 "quick ", "brown ", "fox ",   "jumps ",   "over ",
		"lazy ", "dog ",   "bzip2 ", "block ", "sorting ", "\n"};
	size_t i = 0;
	while (i < n) {
		const char *w = words[rnd() % 12];
		for (size_t j = 0; w[j] && i < n; j++)
			b[i++] = (uint8_t)w[j];
	}
}

/*
 * Valid streams for the decoder: added to the tester's list of streams by
 * its add_stream (stock libbz2, or its randomising build).
 */
static void
seed_dec_valid(void)
{
	const size_t N = 1200000;
	uint8_t *b = calloc(N, 1);
	size_t n = 0;

	add_stream("empty", b, 0, 1, 0);
	add_stream("empty", b, 0, 9, 0);
	b[0] = 'A';
	add_stream("one_byte", b, 1, 9, 0);
	for (int i = 0; i < 512; i++)
		b[i] = (uint8_t)(i < 256 ? i : 511 - i);
	add_stream("all256", b, 512, 9, 0);
	/* runs around the RLE1 lengths, each followed by another byte */
	static const int lens[] = {1,	2,   3,	  4,   5,   6,	  7,   8,
				   254, 255, 256, 257, 258, 259,  260, 261,
				   262, 263, 517, 518, 519, 1000, 4096};
	for (size_t k = 0; k < sizeof lens / sizeof lens[0]; k++) {
		for (int j = 0; j < lens[k]; j++)
			b[n++] = (uint8_t)('a' + k % 20);
		b[n++] = 'Z';
	}
	add_stream("runs", b, n, 9, 0);
	add_stream("runs", b, n, 1, 1);
	for (size_t i = 0; i < 20000; i++)
		b[i] = (uint8_t)rnd();
	add_stream("random_2k", b, 2000, 9, 0);
	add_stream("random_20k", b, 20000, 1, 0);
	/* geometric distribution: long Huffman codes */
	for (size_t i = 0; i < 30000; i++) {
		uint64_t r = rnd();
		int c = 0;
		while ((r & 1) && c < 60) {
			c++;
			r >>= 1;
		}
		b[i] = (uint8_t)c;
	}
	add_stream("skewed_30k", b, 30000, 9, 0);
	seed_text(b, 4000);
	add_stream("text_4k", b, 4000, 5, 0);
	add_stream("text_4k", b, 4000, 1, 1);
	/* three level-1 blocks of text, the first two full */
	seed_text(b, 250000);
	add_stream("text_250k", b, 250000, 1, 0);
	add_stream("text_250k", b, 250000, 1, 1);
	/*
	 * one randomised block longer than the 278,212 bytes after which the
	 * randomisation table starts again
	 */
	seed_text(b, 400000);
	add_stream("text_400k", b, 400000, 5, 1);
	/* exactly periodic: one 300,000-row block */
	for (size_t i = 0; i < 300007; i++)
		b[i] = (uint8_t)("abcabdx"[i % 7]);
	add_stream("period_7", b, 300007, 3, 0);
	memset(b, 'x', 1000000);
	add_stream("one_symbol_1M", b, 1000000, 9, 0);
	/* DNA-like with noise, at every third level */
	for (size_t i = 0; i < 100000; i++)
		b[i] = (uint8_t)(rnd() % 7 == 0 ? rnd() : "ACGT"[(i / 3) % 4]);
	for (int lv = 1; lv <= 9; lv += 4)
		add_stream("dna_100k", b, 100000, lv, 0);
	free(b);
}

/*
 * Streams whose one block ends where the bulk un-RLE1 kernel's whole-vector
 * stores could leave bytes past what is delivered, as the decoder tester's
 * slack phase builds them: literals that put a run at every offset of a
 * kernel step, a run of every length from 4 to 40, then zero to three runs of
 * four.  Each gets one header: fixed output pieces of 1 to 297 bytes, small
 * mode on every other stream.
 */
static void
seed_dec_slack(void)
{
	static const uint8_t outs[] = {0, 4, 15, 16, 32, 99, 255};
	uint8_t t[128], h[8];
	unsigned k = 0;

	for (int lit = 16; lit < 32; lit++)
		for (int run = 4; run <= 40; run++)
			for (int fours = 0; fours <= 3; fours++, k++) {
				size_t n = 0;
				for (int i = 0; i < lit; i++)
					t[n++] = (uint8_t)('a' + i % 23);
				memset(t + n, 'X', (size_t)run);
				n += (size_t)run;
				for (int f = 0; f < fours; f++) {
					memset(t + n, 'p' + f, 4);
					n += 4;
				}
				unsigned cl = 1024;
				uint8_t comp[1024];
				if (ref_BZ2_bzBuffToBuffCompress(
					    (char *)comp, &cl, (char *)t,
					    (unsigned)n, 1, 0, 30) != BZ_OK) {
					fprintf(stderr, "slack seed failed\n");
					exit(2);
				}
				seed_dec_header(h, (int)(k & 1), 1, 1, 255,
						outs[k % 7], 0);
				seed_write("decompress", h, 8, comp, cl);
			}
}

/* The decoder target's seeds. */
static void
seed_decompress(void)
{
	const size_t first = n_streams;

	seed_dec_valid();
	gen_crafted();
	for (size_t i = first; i < n_streams; i++)
		seed_dec_stream(streams[i].comp, streams[i].comp_len);
	/* a stream followed by trailing bytes and a second stream */
	const Stream *s1 = &streams[first + 2], *s2 = &streams[first + 3];
	const size_t len = s1->comp_len + 5 + s2->comp_len;
	uint8_t *cat = malloc(len);
	memcpy(cat, s1->comp, s1->comp_len);
	memcpy(cat + s1->comp_len, "junk!", 5);
	memcpy(cat + s1->comp_len + 5, s2->comp, s2->comp_len);
	seed_dec_stream(cat, len);
	free(cat);
	seed_dec_slack();
}

/*
 * The encoder header (fuzz/fuzz_compress.c): flags in byte 0 (level and
 * schedule), workFactor, the largest input and output pieces, and the flush,
 * error and partial-finish chances.
 */
static void
seed_enc_header(uint8_t h[11], uint8_t flags, uint8_t in_byte, uint8_t out_byte,
		uint8_t p_flush, uint8_t err_byte)
{
	h[0] = flags;
	h[1] = 30;
	h[2] = in_byte;
	h[3] = out_byte;
	h[4] = p_flush;
	h[5] = err_byte;
	h[6] = 20;
	h[7] = (uint8_t)seeds_written;
	h[8] = (uint8_t)(seeds_written >> 8);
	h[9] = 0xC0;
	h[10] = 0xDE;
}

/*
 * Writes data[0..n) behind headers for the plain schedule at levels 1 and 9,
 * a random schedule with flushes and small pieces at level 5, and one with
 * injected sequence errors and zero output space at level 2.
 */
static void
seed_enc_data(const uint8_t *data, size_t n)
{
	uint8_t h[11];

	seed_enc_header(h, 0x20 | 0, 0xFF, 0xFF, 0, 0);
	seed_write("compress", h, 11, data, n);
	seed_enc_header(h, 0x20 | 8, 0xFF, 0x90, 0, 0);
	seed_write("compress", h, 11, data, n);
	seed_enc_header(h, 4, 0x20, 0x10, 60, 0);
	seed_write("compress", h, 11, data, n);
	seed_enc_header(h, 0x10 | 1, 0x80, 0x08, 20, 0x80 | 30);
	seed_write("compress", h, 11, data, n);
}

/* The encoder target's seeds. */
static void
seed_compress(void)
{
	uint8_t *b = malloc(8192);

	seed_enc_data(b, 0);
	b[0] = 'x';
	seed_enc_data(b, 1);
	for (int i = 0; i < 256; i++)
		b[i] = (uint8_t)i;
	seed_enc_data(b, 256);
	/* runs of every length around the RLE1 unit sizes */
	static const int lens[] = {1, 2, 3, 4, 5, 254, 255, 256, 259, 509, 510};
	size_t n = 0;
	for (size_t k = 0; k < sizeof lens / sizeof lens[0]; k++) {
		memset(b + n, 'a' + (int)k, (size_t)lens[k]);
		n += (size_t)lens[k];
	}
	seed_enc_data(b, n);
	seed_text(b, 4096);
	seed_enc_data(b, 4096);
	for (size_t i = 0; i < 3000; i++)
		b[i] = (uint8_t)rnd();
	seed_enc_data(b, 3000);
	memset(b, 0, 3000);
	seed_enc_data(b, 3000);
	for (size_t i = 0; i < 3000; i++)
		b[i] = (uint8_t)("abcabdx"[i % 7]);
	seed_enc_data(b, 3000);
	free(b);
}

/* Writes the sorter input: mode byte, then block[0..n). */
static void
seed_sort_block(uint8_t mode, const uint8_t *block, size_t n)
{
	seed_write("sort", &mode, 1, block, n);
}

/* Length of the Fibonacci word written to out, at most max_len. */
static size_t
seed_fibonacci(uint8_t *out, size_t max_len)
{
	size_t la = 2, lb = 1;

	out[0] = 'a';
	out[1] = 'b';
	while (la + lb <= max_len) {
		memcpy(out + la, out, lb);
		const size_t next = la + lb;
		lb = la;
		la = next;
	}
	return la;
}

/* The sorter target's seeds. */
static void
seed_sort(void)
{
	uint8_t *b = malloc(16384);
	size_t n;

	static const char *const words[] = {"a",      "ab",	     "ba",
					    "aab",    "abab",	     "abcabd",
					    "banana", "mississippi", "zzzzza"};
	for (size_t k = 0; k < sizeof words / sizeof words[0]; k++)
		seed_sort_block(0, (const uint8_t *)words[k], strlen(words[k]));
	/* exactly and nearly periodic, built by the target from short words */
	seed_sort_block(0x80 | 15, (const uint8_t *)"z", 1);
	seed_sort_block(0x80 | 9, (const uint8_t *)"abcabd", 6);
	seed_sort_block(0x80 | 0x30 | 4, (const uint8_t *)"abaab", 5);
	n = seed_fibonacci(b, 2000);
	seed_sort_block(0, b, n);
	seed_sort_block(0, b, n - 1);
	for (size_t i = 0; i < 1024; i++)
		b[i] = (uint8_t)('a' + (__builtin_popcount((unsigned)i) & 1));
	seed_sort_block(0, b, 1024);
	/* de Bruijn B(2, 10) by the prefer-one rule */
	memset(b, 0, 10);
	n = 10;
	static uint8_t seen[1024];
	memset(seen, 0, sizeof seen);
	seen[0] = 1;
	for (;;) {
		unsigned v = 0;
		for (size_t i = n - 9; i < n; i++)
			v = v << 1 | b[i];
		if (!seen[(v << 1 | 1) & 1023]) {
			b[n++] = 1;
			seen[(v << 1 | 1) & 1023] = 1;
		} else if (!seen[(v << 1) & 1023]) {
			b[n++] = 0;
			seen[(v << 1) & 1023] = 1;
		} else {
			break;
		}
	}
	seed_sort_block(0, b, n);
	static const int sigmas[] = {2, 4, 256};
	static const size_t sizes[] = {50, 1000, 10000};
	for (size_t s = 0; s < 3; s++)
		for (size_t z = 0; z < 3; z++) {
			for (size_t i = 0; i < sizes[z]; i++)
				b[i] = (uint8_t)(rnd() % (uint64_t)sigmas[s]);
			seed_sort_block(0, b, sizes[z]);
		}
	/* runs of random lengths */
	n = 0;
	while (n < 5000) {
		const size_t run = 1 + rnd() % 300;
		const uint8_t c = (uint8_t)(rnd() % 3);
		for (size_t j = 0; j < run && n < 5000; j++)
			b[n++] = c;
	}
	seed_sort_block(0, b, n);
	/* zigzag: low, high, low, ... */
	for (size_t i = 0; i < 1000; i++)
		b[i] = (uint8_t)(i & 1 ? 128 + rnd() % 128 : rnd() % 8);
	seed_sort_block(0, b, 1000);
	seed_text(b, 2000);
	seed_sort_block(0, b, 2000);
	free(b);
}

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: fuzz_seeds DIR\n");
		return 2;
	}
	seed_dir = argv[1];
	seed_mkdir(seed_dir);
	static const char *const targets[] = {"decompress", "compress", "sort"};
	for (int t = 0; t < 3; t++) {
		char path[1024];
		snprintf(path, sizeof path, "%s/%s", seed_dir, targets[t]);
		seed_mkdir(path);
	}

	rs = seed;
	seed_decompress();
	seed_compress();
	seed_sort();
	printf("fuzz_seeds: %lu seeds in %s\n", seeds_written, seed_dir);
	return 0;
}
