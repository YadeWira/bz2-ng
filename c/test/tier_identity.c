/*
 * tier_identity.c - a digest of everything bz2-ng writes and reads
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * Compresses a fixed set of generated inputs at levels 1, 5 and 9 and
 * decompresses the streams (normal and small mode), and prints the kernel
 * tier and one 64-bit digest of every byte produced.  tier_identity.cmake
 * runs it under every tier the CPU supports and requires the digests to be
 * equal: every tier must produce the same bytes in both directions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bzlib.h"
#include "kern.h"
#include "test_tier.h"

static u64 digest = 0xcbf29ce484222325;

/* FNV-1a over p[0..n), folded into the running digest. */
static void
mix(const void *p, size_t n)
{
	const u8 *b = p;

	for (size_t i = 0; i < n; i++)
		digest = (digest ^ b[i]) * 0x100000001b3;
}

/* Fills buf[0..n) with data of the given kind from seed. */
static void
generate(u8 *buf, size_t n, int kind, u64 seed)
{
	static const char *words[] = {"the ",	"quick ", "brown ", "fox\n",
				      "bzip2 ", "block ", "sort "};

	for (size_t i = 0; i < n;) {
		seed ^= seed << 13;
		seed ^= seed >> 7;
		seed ^= seed << 17;
		if (kind == 0) {
			buf[i++] = (u8)seed;
		} else if (kind == 1) {
			const char *w = words[seed % 7];

			for (size_t j = 0; w[j] && i < n; j++)
				buf[i++] = (u8)w[j];
		} else if (kind == 2) {
			size_t run = 1 + seed % 300;

			for (size_t j = 0; j < run && i < n; j++)
				buf[i++] = (u8)(seed >> 40) & 7;
		} else {
			buf[i++] = "ACGT"[seed & 3];
		}
	}
}

int
main(void)
{
	static const size_t sizes[] = {0, 1, 100, 5000, 70000, 250000, 1100000};
	u8 *in = malloc(1100000);
	u8 *comp = malloc(1200000);
	u8 *out = malloc(1100001);

	require_requested_tier();
	if (in == NULL || comp == NULL || out == NULL) {
		printf("out of memory\n");
		return 1;
	}

	for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
		for (int kind = 0; kind < 4; kind++) {
			generate(in, sizes[s], kind, 0x9E3779B97F4A7C15 + s);
			for (int level = 1; level <= 9; level += 4) {
				unsigned clen = 1200000, dlen = 1100001;

				int rc = BZ2_bzBuffToBuffCompress(
					(char *)comp, &clen, (char *)in,
					(unsigned)sizes[s], level, 0, 0);
				if (rc != BZ_OK) {
					printf("compress failed (%d): size %zu "
					       "kind %d level %d\n",
					       rc, sizes[s], kind, level);
					return 1;
				}
				mix(comp, clen);
				for (int small = 0; small <= 1; small++) {
					dlen = 1100001;
					rc = BZ2_bzBuffToBuffDecompress(
						(char *)out, &dlen,
						(char *)comp, clen, small, 0);
					if (rc != BZ_OK || dlen != sizes[s] ||
					    memcmp(out, in, dlen) != 0) {
						printf("round trip failed "
						       "(%d): "
						       "size %zu kind %d level "
						       "%d small %d\n",
						       rc, sizes[s], kind,
						       level, small);
						return 1;
					}
					mix(out, dlen);
				}
			}
		}
	}
	printf("tier=%s digest=%016llx\n", bzng_tier_name(),
	       (unsigned long long)digest);
	free(in);
	free(comp);
	free(out);
	return 0;
}
