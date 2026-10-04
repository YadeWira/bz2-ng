/*
 * bench.c - speed of bz2-ng against stock libbz2 through the legacy API
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * usage: bench [-r rounds] [-l level] [-c chunk] [-n max_bytes] [-o out.tsv]
 *              name=file[,file...] ...
 *
 * Each data set is the given files cut into independent streams of chunk
 * bytes (0: each file is one stream), at most max_bytes per file.  Per stream
 * and implementation: BZ2_bzCompressInit, one BZ2_bzCompress(BZ_FINISH) over
 * the whole stream, BZ2_bzCompressEnd; and BZ2_bzDecompressInit, one
 * BZ2_bzDecompress over the whole stream, BZ2_bzDecompressEnd.  Stock libbz2
 * 1.0.8 (test/reference, compiled at its Makefile's -O2) is linked under the
 * ref_ prefix.  bz2-ng's decoder is timed with both memory layouts.
 *
 * Before timing, every stream is checked: bz2-ng's output must equal
 * libbz2's byte for byte, and every decoder must reproduce the input.  Each
 * round then runs every implementation once, in an order rotated per round.
 * One TSV line per (set, implementation, operation, round): MB/s, with MB =
 * 10^6 bytes of uncompressed data.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "dec.h"
#include "kern.h"
#include "reference_api.h"

struct stream {
	const u8 *raw;
	size_t raw_len;
	u8 *comp;
	size_t comp_len;
};

struct dataset {
	char name[64];
	struct stream *streams;
	size_t n;
	size_t total;
	size_t max_raw, max_comp;
};

static int level = 9;

/* Implementations timed. */
enum {
	REF_C,
	NG_C,
	REF_D,
	NG_D_FIT,
	NG_D_WIDE,
	N_IMPL
};
static const char *impl_name[N_IMPL] = {"libbz2", "bz2ng", "libbz2",
					"bz2ng_fit", "bz2ng_wide"};
static const char *impl_op[N_IMPL] = {"c", "c", "d", "d", "d"};

/* Exits with a message naming the program. */
static void
die(const char *fmt, const char *arg)
{
	fprintf(stderr, "bench: ");
	fprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	exit(2);
}

/* malloc or realloc that exits on failure. */
static void *
xrealloc(void *p, size_t n)
{
	p = realloc(p, n ? n : 1);
	if (p == NULL)
		die("out of memory (%s)", "realloc");
	return p;
}

/*
 * Exits after call what of implementation impl returned rc for stream i of
 * set.
 */
static void
die_stream(const char *set, size_t i, const char *impl, const char *what,
	   int rc)
{
	fprintf(stderr, "bench: %s stream %zu: %s %s returned %d\n", set, i,
		impl, what, rc);
	exit(3);
}

/* Seconds on a monotonic clock. */
static double
now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* Reads at most max bytes of path; *n receives the length. */
static u8 *
read_file(const char *path, size_t max, size_t *n)
{
	FILE *f = fopen(path, "rb");
	size_t cap = 1 << 20, len = 0;
	u8 *buf;

	if (f == NULL) {
		fprintf(stderr, "bench: cannot open ");
		perror(path);
		exit(2);
	}
	buf = xrealloc(NULL, cap);
	for (;;) {
		size_t want = cap - len, got;

		if (max && len + want > max)
			want = max - len;
		got = fread(buf + len, 1, want, f);
		len += got;
		if (got == 0 || (max && len >= max))
			break;
		if (len == cap)
			buf = xrealloc(buf, cap *= 2);
	}
	if (ferror(f)) {
		fprintf(stderr, "bench: cannot read ");
		perror(path);
		exit(2);
	}
	fclose(f);
	*n = len;
	return buf;
}

/*
 * Compresses in[0..n) into out (cap bytes) with the given API.  Returns
 * BZ_STREAM_END and the compressed length in *len, or the return code of the
 * call that failed (*what names it).
 */
static int
compress_one(int (*init)(bz_stream *, int, int, int),
	     int (*compress)(bz_stream *, int), int (*end)(bz_stream *),
	     const u8 *in, size_t n, u8 *out, size_t cap, size_t *len,
	     const char **what)
{
	bz_stream s;
	int rc;

	memset(&s, 0, sizeof(s));
	rc = init(&s, level, 0, 0);
	if (rc != BZ_OK) {
		*what = "BZ2_bzCompressInit";
		return rc;
	}
	s.next_in = (char *)in;
	s.avail_in = (unsigned)n;
	s.next_out = (char *)out;
	s.avail_out = (unsigned)cap;
	rc = compress(&s, BZ_FINISH);
	end(&s);
	*what = "BZ2_bzCompress";
	*len = cap - s.avail_out;
	return rc;
}

/*
 * Decompresses comp[0..n) into out (cap bytes); layout < 0 is libbz2, else
 * bz2-ng with that layout.  Returns BZ_STREAM_END and the decompressed length
 * in *len, or the return code of the call that failed (*what names it).
 */
static int
decompress_one(int layout, const u8 *comp, size_t n, u8 *out, size_t cap,
	       size_t *len, const char **what)
{
	bz_stream s;
	int rc;

	memset(&s, 0, sizeof(s));
	rc = layout < 0 ? ref_BZ2_bzDecompressInit(&s, 0, 0) :
			  bzng_decompress_init(&s, 0, 0, layout);
	if (rc != BZ_OK) {
		*what = "BZ2_bzDecompressInit";
		return rc;
	}
	s.next_in = (char *)comp;
	s.avail_in = (unsigned)n;
	s.next_out = (char *)out;
	s.avail_out = (unsigned)cap;
	rc = layout < 0 ? ref_BZ2_bzDecompress(&s) : BZ2_bzDecompress(&s);
	if (layout < 0)
		ref_BZ2_bzDecompressEnd(&s);
	else
		BZ2_bzDecompressEnd(&s);
	*what = "BZ2_bzDecompress";
	*len = cap - s.avail_out;
	return rc;
}

/* Loads a data set "name=file[,file...]", cut into streams, compressed by
 * libbz2, and checks bz2-ng against it. */
static void
load_set(struct dataset *d, const char *spec, size_t chunk, size_t max)
{
	const char *eq = strchr(spec, '=');
	char *files, *tok, *save = NULL;
	size_t cap = 16;

	if (eq == NULL || eq == spec || eq[1] == '\0')
		die("bad data set \"%s\" (expected name=file[,file...])", spec);
	snprintf(d->name, sizeof(d->name), "%.*s", (int)(eq - spec), spec);
	d->streams = xrealloc(NULL, cap * sizeof(*d->streams));
	d->n = d->total = d->max_raw = d->max_comp = 0;
	files = xrealloc(NULL, strlen(eq + 1) + 1);
	strcpy(files, eq + 1);
	for (tok = strtok_r(files, ",", &save); tok;
	     tok = strtok_r(NULL, ",", &save)) {
		size_t len, off;
		u8 *data = read_file(tok, max, &len);
		size_t step = chunk ? chunk : (len ? len : 1);

		for (off = 0; off < len; off += step) {
			struct stream *s;

			if (d->n == cap)
				d->streams = xrealloc(d->streams,
						      (cap *= 2) * sizeof(*s));
			s = &d->streams[d->n++];
			s->raw = data + off;
			s->raw_len = len - off < step ? len - off : step;
			d->total += s->raw_len;
			if (s->raw_len > d->max_raw)
				d->max_raw = s->raw_len;
		}
	}
	free(files);
	for (size_t i = 0; i < d->n; i++) {
		struct stream *s = &d->streams[i];
		size_t cap2 = s->raw_len + s->raw_len / 50 + 4096;
		u8 *mine = xrealloc(NULL, cap2);
		u8 *dec = xrealloc(NULL, s->raw_len + 1);
		size_t mlen = 0, dlen = 0;
		const char *what = "";
		int rc;

		s->comp = xrealloc(NULL, cap2);
		rc = compress_one(ref_BZ2_bzCompressInit, ref_BZ2_bzCompress,
				  ref_BZ2_bzCompressEnd, s->raw, s->raw_len,
				  s->comp, cap2, &s->comp_len, &what);
		if (rc != BZ_STREAM_END)
			die_stream(d->name, i, "libbz2", what, rc);
		rc = compress_one(BZ2_bzCompressInit, BZ2_bzCompress,
				  BZ2_bzCompressEnd, s->raw, s->raw_len, mine,
				  cap2, &mlen, &what);
		if (rc != BZ_STREAM_END)
			die_stream(d->name, i, "bz2-ng", what, rc);
		if (mlen != s->comp_len || memcmp(mine, s->comp, mlen) != 0) {
			fprintf(stderr,
				"bench: %s stream %zu: bz2-ng's compressed "
				"bytes differ from libbz2's\n",
				d->name, i);
			exit(6);
		}
		for (int layout = -1; layout <= 1; layout++) {
			rc = decompress_one(layout, s->comp, s->comp_len, dec,
					    s->raw_len + 1, &dlen, &what);
			if (rc != BZ_STREAM_END)
				die_stream(d->name, i,
					   impl_name[REF_D + 1 + layout], what,
					   rc);
			if (dlen != s->raw_len ||
			    memcmp(dec, s->raw, s->raw_len) != 0) {
				fprintf(stderr,
					"bench: %s stream %zu: %s does not "
					"reproduce the input\n",
					d->name, i,
					impl_name[REF_D + 1 + layout]);
				exit(6);
			}
		}
		if (s->comp_len > d->max_comp)
			d->max_comp = s->comp_len;
		free(mine);
		free(dec);
	}
}

/* Seconds to run implementation impl over every stream of d. */
static double
time_set(const struct dataset *d, int impl, u8 *buf, size_t cap)
{
	double t0 = now();

	for (size_t i = 0; i < d->n; i++) {
		const struct stream *s = &d->streams[i];
		const char *what = "";
		size_t len;
		int rc;

		switch (impl) {
		case REF_C:
			rc = compress_one(ref_BZ2_bzCompressInit,
					  ref_BZ2_bzCompress,
					  ref_BZ2_bzCompressEnd, s->raw,
					  s->raw_len, buf, cap, &len, &what);
			break;
		case NG_C:
			rc = compress_one(BZ2_bzCompressInit, BZ2_bzCompress,
					  BZ2_bzCompressEnd, s->raw, s->raw_len,
					  buf, cap, &len, &what);
			break;
		case REF_D:
			rc = decompress_one(-1, s->comp, s->comp_len, buf, cap,
					    &len, &what);
			break;
		case NG_D_FIT:
			rc = decompress_one(BZNG_LAYOUT_FIT, s->comp,
					    s->comp_len, buf, cap, &len, &what);
			break;
		default:
			rc = decompress_one(BZNG_LAYOUT_WIDE, s->comp,
					    s->comp_len, buf, cap, &len, &what);
			break;
		}
		if (rc != BZ_STREAM_END)
			die_stream(d->name, i, impl_name[impl], what, rc);
	}
	return now() - t0;
}

/*
 * The value of numeric option -flag: a whole number in [lo, hi], or an exit
 * with a message naming the flag.
 */
static unsigned long long
parse_number(int flag, const char *arg, unsigned long long lo,
	     unsigned long long hi)
{
	char *end;
	unsigned long long v = strtoull(arg, &end, 0);

	if (*arg == '\0' || *arg == '-' || *end != '\0' || v < lo || v > hi) {
		fprintf(stderr,
			"bench: -%c wants a number from %llu to %llu, "
			"not \"%s\"\n",
			flag, lo, hi, arg);
		exit(2);
	}
	return v;
}

static void
usage(void)
{
	fprintf(stderr, "usage: bench [-r rounds] [-l level] [-c chunk] "
			"[-n max_bytes] [-o out.tsv]\n"
			"             name=file[,file...] ...\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	int rounds = 5, opt;
	size_t chunk = 0, max = 0;
	FILE *out = stdout;
	struct dataset *sets;
	int nsets;

	while ((opt = getopt(argc, argv, "r:l:c:n:o:")) != -1) {
		switch (opt) {
		case 'r':
			rounds = (int)parse_number('r', optarg, 1, 1000000);
			break;
		case 'l':
			level = (int)parse_number('l', optarg, 1, 9);
			break;
		case 'c':
			chunk = (size_t)parse_number('c', optarg, 0,
						     UINT32_MAX / 2);
			break;
		case 'n':
			max = (size_t)parse_number('n', optarg, 0, SIZE_MAX);
			break;
		case 'o':
			out = fopen(optarg, "a");
			if (out == NULL) {
				fprintf(stderr, "bench: cannot open ");
				perror(optarg);
				return 2;
			}
			break;
		default:
			usage();
		}
	}
	nsets = argc - optind;
	if (nsets == 0)
		usage();
	sets = xrealloc(NULL, (size_t)nsets * sizeof(*sets));
	for (int i = 0; i < nsets; i++)
		load_set(&sets[i], argv[optind + i], chunk, max);
	for (int r = 0; r < rounds; r++) {
		for (int i = 0; i < nsets; i++) {
			const struct dataset *d = &sets[i];
			size_t cap = d->max_raw + d->max_raw / 50 + 4096;
			u8 *buf = xrealloc(NULL, cap);

			for (int k = 0; k < N_IMPL; k++) {
				int impl = (k + r) % N_IMPL;
				double t = time_set(d, impl, buf, cap);

				fprintf(out,
					"%s\t%d\t%zu\t%s\t%s\t%s\t%d\t%.2f\n",
					d->name, level, chunk, bzng_tier_name(),
					impl_name[impl], impl_op[impl], r,
					d->total / t / 1e6);
				fflush(out);
			}
			free(buf);
		}
	}
	if (fflush(out) != 0 || ferror(out)) {
		perror("bench: writing the results");
		return 2;
	}
	return 0;
}
