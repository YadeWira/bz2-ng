/*
 * cli_tool.c - input generator for cli_compare.sh
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * usage: cli_tool rand-compress < in > out   randomised-block bzip2 stream
 *        cli_tool flip FILE BIT > out         FILE with bit BIT inverted
 *        cli_tool text N > out                N bytes of generated text
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reference_api.h"

/* Reads all of f into a malloc'd buffer; *n receives its length. */
static unsigned char *
read_all(FILE *f, size_t *n)
{
	size_t cap = 1 << 16, len = 0;
	unsigned char *buf = malloc(cap);

	for (;;) {
		if (buf == NULL) {
			fprintf(stderr, "cli_tool: out of memory\n");
			exit(2);
		}
		size_t got = fread(buf + len, 1, cap - len, f);

		len += got;
		if (got == 0)
			break;
		if (len == cap)
			buf = realloc(buf, cap *= 2);
	}
	*n = len;
	return buf;
}

int
main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "rand-compress") == 0) {
		size_t n;
		unsigned char *in = read_all(stdin, &n);
		unsigned cl = (unsigned)(n + n / 50 + 1000);
		unsigned char *out = malloc(cl);
		int rc;

		if (out == NULL) {
			fprintf(stderr, "cli_tool: out of memory\n");
			return 2;
		}
		rc = rand_BZ2_bzBuffToBuffCompress((char *)out, &cl, (char *)in,
						   (unsigned)n, 9, 0, 30);
		if (rc != BZ_OK) {
			fprintf(stderr, "cli_tool: rand-compress failed (%d)\n",
				rc);
			return 1;
		}
		fwrite(out, 1, cl, stdout);
		return 0;
	}
	if (argc == 4 && strcmp(argv[1], "flip") == 0) {
		FILE *f = fopen(argv[2], "rb");
		size_t n, bit = strtoul(argv[3], NULL, 10);
		unsigned char *buf;

		if (f == NULL) {
			perror(argv[2]);
			return 2;
		}
		buf = read_all(f, &n);
		fclose(f);
		if (bit / 8 >= n) {
			fprintf(stderr,
				"cli_tool: bit %zu is past the end of "
				"%s (%zu bytes)\n",
				bit, argv[2], n);
			return 1;
		}
		buf[bit / 8] ^= (unsigned char)(1 << (bit % 8));
		fwrite(buf, 1, n, stdout);
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "text") == 0) {
		static const char *words[] = {"the ",  "quick ", "brown ",
					      "fox ",  "jumps ", "over ",
					      "lazy ", "dogs\n"};
		size_t n = strtoul(argv[2], NULL, 10);
		unsigned long long x = 0x9E3779B97F4A7C15ull;

		for (size_t i = 0; i < n;) {
			const char *w;

			x ^= x << 13;
			x ^= x >> 7;
			x ^= x << 17;
			w = words[x % 8];
			for (size_t j = 0; w[j] && i < n; j++, i++)
				putchar(w[j]);
		}
		return 0;
	}
	fprintf(stderr, "usage: cli_tool rand-compress | flip FILE BIT | "
			"text N\n");
	return 2;
}
