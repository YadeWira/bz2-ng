/*
 * sort_oracle.h - an independent oracle for the block sorter
 *
 * Cyclic prefix doubling with radix sorts, and a naive rotation comparison
 * for cross-checking it on short blocks; shared by test/sort_test.c and
 * fuzz/fuzz_sort.c.
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

#ifndef TEST_SORT_ORACLE_H
#define TEST_SORT_ORACLE_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int32_t *o_sa, *o_rank, *o_tmp, *o_key, *o_cnt;

/* Allocates the oracle's arrays for blocks of up to max_n bytes. */
static inline void
oracle_alloc(int32_t max_n)
{
	o_sa = malloc((size_t)max_n * sizeof(int32_t));
	o_rank = malloc((size_t)max_n * sizeof(int32_t));
	o_tmp = malloc((size_t)max_n * sizeof(int32_t));
	o_key = malloc((size_t)max_n * sizeof(int32_t));
	o_cnt = malloc(((size_t)max_n + 257) * sizeof(int32_t));
}

/* Stable counting sort of idx[0..n) into out by key[idx[i]] in [0, k). */
static inline void
counting_sort(const int32_t *idx, int32_t *out, const int32_t *key, int32_t n,
	      int32_t k)
{
	memset(o_cnt, 0, (size_t)(k + 1) * sizeof(int32_t));
	for (int32_t i = 0; i < n; i++)
		o_cnt[key[idx[i]] + 1]++;
	for (int32_t c = 0; c < k; c++)
		o_cnt[c + 1] += o_cnt[c];
	for (int32_t i = 0; i < n; i++)
		out[o_cnt[key[idx[i]]]++] = idx[i];
}

/* Sorted rotations of T in o_sa (ties in any order), ranks in o_rank (equal
 * rotations share a rank). Returns the number of distinct ranks. */
static inline int32_t
oracle_sort(const uint8_t *T, int32_t n)
{
	for (int32_t i = 0; i < n; i++) {
		o_tmp[i] = i;
		o_rank[i] = T[i];
	}
	counting_sort(o_tmp, o_sa, o_rank, n, 256);
	int32_t classes = 0;
	for (int32_t i = 0; i < n; i++) {
		if (i == 0 || T[o_sa[i]] != T[o_sa[i - 1]])
			classes++;
		o_key[o_sa[i]] = classes - 1;
	}
	memcpy(o_rank, o_key, (size_t)n * sizeof(int32_t));
	for (int32_t h = 1; h < n && classes < n; h *= 2) {
		/* Sort by (rank[i], rank[i+h]): by the second key via the
		 * shifted order of the current SA, then stably by the first. */
		for (int32_t i = 0; i < n; i++) {
			int32_t j = o_sa[i] - h;
			o_tmp[i] = j < 0 ? j + n : j;
		}
		counting_sort(o_tmp, o_sa, o_rank, n, classes);
		classes = 0;
		for (int32_t i = 0; i < n; i++) {
			int32_t a = o_sa[i], b = i ? o_sa[i - 1] : 0;
			int32_t an = a + h >= n ? a + h - n : a + h,
				bn = b + h >= n ? b + h - n : b + h;
			if (i == 0 || o_rank[a] != o_rank[b] ||
			    o_rank[an] != o_rank[bn])
				classes++;
			o_key[a] = classes - 1;
		}
		memcpy(o_rank, o_key, (size_t)n * sizeof(int32_t));
	}
	return classes;
}

/* Naive rotation comparison, for cross-checking the oracle on short blocks. */
static const uint8_t *naive_T;
static int32_t naive_n;
static inline int
naive_cmp(const void *pa, const void *pb)
{
	int32_t a = *(const int32_t *)pa, b = *(const int32_t *)pb;
	for (int32_t k = 0; k < naive_n; k++) {
		uint8_t ca = naive_T[(a + k) % naive_n],
			cb = naive_T[(b + k) % naive_n];
		if (ca != cb)
			return ca < cb ? -1 : 1;
	}
	return 0;
}

#endif /* TEST_SORT_ORACLE_H */
