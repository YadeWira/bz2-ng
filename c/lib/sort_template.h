/*
 * sort_template.h - induced-sorting kernels of the block sorter
 *
 * Derived from sais.c for sais-lite
 * Copyright (c) 2008-2010 Yuta Mori All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * ALTERED for bz2-ng: induced-sorting kernels of sais-lite 2.4.1, rewritten
 * as a template over the symbol type and the bucket-pointer mode. sort.c
 * includes this file four times: SYM = u8 for the block itself
 * (SORT_TOP 1) and SYM = s32 for the reduced problems of the recursion
 * (SORT_TOP 0), each with SORT_REGISTER_BUCKET 0 and 1. Each function is
 * sais-lite's of the same name with the symbol width fixed at compile time.
 *
 * Bucket-pointer modes: with SORT_REGISTER_BUCKET 1 a scan keeps the current
 * bucket's pointer in a register and writes it back when the bucket changes
 * (sais-lite's way; a branch per step, mispredicted at every change, so it
 * suits repetitive blocks). With 0 it updates B[c] in memory every step (no
 * branch, but a store-to-load chain while the bucket repeats).
 */

#if SORT_REGISTER_BUCKET
#  define SORT_BUCKET_BEGIN() (b = SA + B[c1])
#  define SORT_BUCKET_SWITCH(e)                  \
	  do {                                   \
		  if ((e) != c1) {               \
			  B[c1] = (s32)(b - SA); \
			  b = SA + B[c1 = c0];   \
		  }                              \
	  } while (0)
#  define SORT_PUT_INC(v)     (*b++ = (v))
#  define SORT_PUT_DEC(v)     (*--b = (v))
#  define SORT_POS_INC()      ((s32)(b - SA))
#  define SORT_POS_DEC()      ((s32)(b - SA) - 1)
#  define SORT_BUCKET_FLUSH() ((void)0)
#else
#  define SORT_BUCKET_BEGIN() ((void)b)
#  define SORT_BUCKET_SWITCH(e) \
	  do {                  \
		  (void)(e);    \
		  c1 = c0;      \
	  } while (0)
#  define SORT_PUT_INC(v)     (SA[B[c1]++] = (v))
#  define SORT_PUT_DEC(v)     (SA[--B[c1]] = (v))
#  define SORT_POS_INC()      (B[c1])
#  define SORT_POS_DEC()      (B[c1] - 1)
#  define SORT_BUCKET_FLUSH() ((void)b)
#endif

/*
 * Name-group bookkeeping of the stage-1 scans: an entry j >= n starts a new
 * group of equal LMS substrings (SORT_GROUP_START), and the first suffix a
 * group writes to a bucket from a source of type t gets the +n mark
 * (SORT_GROUP_FIRST).  SORT_BRANCHLESS_NAMING selects and stores
 * unconditionally instead of branching (sort.c chooses it per platform).
 */
#if SORT_BRANCHLESS_NAMING
#  define SORT_GROUP_START(j, d)             \
	  do {                               \
		  const s32 f_ = n <= (j);   \
		  (d) += f_;                 \
		  (j) -= SORT_SEL(f_, n, 0); \
	  } while (0)
#  define SORT_GROUP_FIRST(j, t, d)                   \
	  do {                                        \
		  (j) += SORT_SEL(D[t] != (d), n, 0); \
		  D[t] = (d);                         \
	  } while (0)
#else
#  define SORT_GROUP_START(j, d)    \
	  do {                      \
		  if (n <= (j)) {   \
			  (d) += 1; \
			  (j) -= n; \
		  }                 \
	  } while (0)
#  define SORT_GROUP_FIRST(j, t, d)   \
	  do {                        \
		  if (D[t] != (d)) {  \
			  (j) += n;   \
			  D[t] = (d); \
		  }                   \
	  } while (0)
#endif

/* Symbol counts of T[0..n) over [0, k) into C. */
static void
FN(get_counts)(const SYM *T, s32 *C, s32 n, s32 k)
{
	if (sizeof(SYM) == 1 && k == 256) {
		/*
		 * Four interleaved histograms, so runs of one byte do not
		 * serialise on a single counter.
		 */
		s32 H[4][256];
		memset(H, 0, sizeof H);
		s32 i = 0;
		for (; i + 4 <= n; i += 4) {
			++H[0][T[i]];
			++H[1][T[i + 1]];
			++H[2][T[i + 2]];
			++H[3][T[i + 3]];
		}
		for (; i < n; ++i)
			++H[0][T[i]];
		for (s32 c = 0; c < 256; ++c)
			C[c] = H[0][c] + H[1][c] + H[2][c] + H[3][c];
		return;
	}
	for (s32 i = 0; i < k; ++i)
		C[i] = 0;
	for (s32 i = 0; i < n; ++i)
		++C[T[i]];
}

/* Bucket starts (end = 0) or ends (end = 1) from the counts C[0..k). */
static void
FN(get_buckets)(const s32 *C, s32 *B, s32 k, int end)
{
	s32 sum = 0;
	if (end) {
		for (s32 i = 0; i < k; ++i) {
			sum += C[i];
			B[i] = sum;
		}
	} else {
		for (s32 i = 0; i < k; ++i) {
			sum += C[i];
			B[i] = sum - C[i];
		}
	}
}

/*
 * The top level always reduces in the in-memory mode (the mode is chosen
 * during its reduction), so its register-mode instantiation needs only the
 * S-scan of lms_sort2 and compute_bwt.
 */
#define SORT_HAS_REDUCE (!SORT_TOP || !SORT_REGISTER_BUCKET)

/*
 * Whether the L-scans of this instantiation may step over each bucket's
 * empty S slots: the top level only, whose seed starts are recorded in its
 * free space, and the in-memory mode only, where the live head B[c] is the
 * end of each bucket's L part.
 */
#define SORT_SKIP (SORT_SKIP_EMPTY_SLOTS && SORT_TOP && !SORT_REGISTER_BUCKET)

#if SORT_HAS_REDUCE
/*
 * Sorts the LMS substrings by induction, naming them in the same passes:
 * lms_sort2_l is the L-scan plus moving the group marks to the other end of
 * each group, lms_sort2_s the S-scan. D[2c + t] holds the pass counter d of
 * the last suffix written to bucket c from a source of type t, and an entry
 * gets +n when it starts a new name group. Seeds must be in place with the
 * group starts already marked. Returns the pass counter for the S-scan and
 * sets *repetitive when fewer than 1 in 10 induced steps changed bucket
 * (counted at the top level only). seed_start (may be NULL; used only where
 * SORT_SKIP) gives where each bucket's seeds start.
 */
static s32
FN(lms_sort2_l)(const SYM *T, s32 *SA, const s32 *C, s32 *B, s32 *D, s32 n,
		s32 k, int *repetitive, const s32 *seed_start)
{
	s32 *b = NULL, i, j, t, d;
	s32 c0, c1;
	s32 changes = 0, induced = 0, cprev = -1;

	FN(get_buckets)(C, B, k, 0);
	j = n - 1;
	c1 = T[j];
	SORT_BUCKET_BEGIN();
	--j;
	t = (T[j] < c1);
	j += n;
	SORT_PUT_INC((t & 1) ? ~j : j);
#  define SORT_LMS2_L_VISIT(slot)                               \
	  do {                                                  \
		  if (0 < (j = SA[slot])) {                     \
			  SORT_GROUP_START(j, d);               \
			  if (SORT_TOP) {                       \
				  changes += T[j] != cprev;     \
				  cprev = T[j];                 \
				  induced += 1;                 \
			  }                                     \
			  SORT_BUCKET_SWITCH(c0 = T[j]);        \
			  --j;                                  \
			  t = c0;                               \
			  t = (t << 1) | (T[j] < c1);           \
			  SORT_GROUP_FIRST(j, t, d);            \
			  SORT_PUT_INC(SORT_MARK_IF(t & 1, j)); \
			  SA[slot] = 0;                         \
		  } else if (j < 0) {                           \
			  SA[slot] = ~j;                        \
		  }                                             \
	  } while (0)
	d = 0;
#  if SORT_SKIP
	if (seed_start != NULL) {
		/*
		 * Each bucket's L part, whose live head B[c] grows while it
		 * is scanned, then its LMS seeds; the empty S slots between
		 * them are never visited.
		 */
		s32 start = 0;
		for (s32 c = 0; c < k; ++c) {
			const s32 end = start + C[c];
			for (i = start; i < B[c]; ++i)
				SORT_LMS2_L_VISIT(i);
			for (i = seed_start[c]; i < end; ++i)
				SORT_LMS2_L_VISIT(i);
			start = end;
		}
	} else {
		for (i = 0; i < n; ++i)
			SORT_LMS2_L_VISIT(i);
	}
#  else
	(void)seed_start;
	for (i = 0; i < n; ++i)
		SORT_LMS2_L_VISIT(i);
#  endif
#  undef SORT_LMS2_L_VISIT
	SORT_BUCKET_FLUSH();
	for (i = n - 1; 0 <= i; --i) {
		if (0 < SA[i]) {
			if (SA[i] < n) {
				SA[i] += n;
				for (j = i - 1; SA[j] < n; --j) {
				}
				SA[j] -= n;
				i = j;
			}
		}
	}
	*repetitive = 10 * (s64)changes < induced;
	(void)cprev;
	return d;
}

#endif

static void
FN(lms_sort2_s)(const SYM *T, s32 *SA, const s32 *C, s32 *B, s32 *D, s32 n,
		s32 k, s32 d)
{
	s32 *b = NULL, i, j = 0, t;
	s32 c0, c1;
	(void)j;

	FN(get_buckets)(C, B, k, 1);
	c1 = 0;
	SORT_BUCKET_BEGIN();
	for (i = n - 1, d += 1; 0 <= i; --i) {
		if (0 < (j = SA[i])) {
			SORT_GROUP_START(j, d);
			SORT_BUCKET_SWITCH(c0 = T[j]);
			--j;
			t = c0;
			t = (t << 1) | (T[j] > c1);
			SORT_GROUP_FIRST(j, t, d);
			SORT_PUT_DEC(SORT_SEL(t & 1, ~(j + 1), j));
			SA[i] = 0;
		}
	}
	SORT_BUCKET_FLUSH();
}

#if SORT_HAS_REDUCE
/*
 * Compacts the sorted LMS substrings into SA[0..m) and, when names are not
 * unique, stores name + 1 of the LMS substring at p in SA[m + p/2]. Returns
 * the number of names. After lms_sort2 the slots hold 0 or a marked
 * (negative) LMS substring; a non-negative slot writes a placeholder to SA[d]
 * that the next marked one overwrites, so the loop has no data-dependent
 * branch.
 */
static s32
FN(lms_postproc2)(s32 *SA, s32 n, s32 m)
{
	s32 i, j, d, name;

	for (i = 0, d = 0, name = 0; d < m; ++i) {
		s32 v = SA[i], neg = v < 0;
		j = ~v;
		name += neg & (j >= n);
		SA[i] = 0;
		SA[d] = j;
		d += neg;
	}
	if (name < m) {
		for (i = m - 1, d = name + 1; 0 <= i; --i) {
			j = SA[i];
			s32 f = j >= n;
			j -= f ? n : 0;
			d -= f;
			SA[m + (j >> 1)] = d;
		}
	} else {
		for (i = 0; i < m; ++i) {
			j = SA[i];
			SA[i] = j >= n ? j - n : j;
		}
	}
	return name;
}

/*
 * Sorts the LMS substrings by induction without naming them (used when the
 * free space has no room for lms_sort2's D array).
 */
static void
FN(lms_sort1)(const SYM *T, s32 *SA, s32 *C, s32 *B, s32 n, s32 k)
{
	s32 *b = NULL, i, j;
	s32 c0, c1;

	if (C == B)
		FN(get_counts)(T, C, n, k);
	FN(get_buckets)(C, B, k, 0);
	j = n - 1;
	c1 = T[j];
	SORT_BUCKET_BEGIN();
	--j;
	SORT_PUT_INC((T[j] < c1) ? ~j : j);
	for (i = 0; i < n; ++i) {
		if (0 < (j = SA[i])) {
			SORT_BUCKET_SWITCH(c0 = T[j]);
			--j;
			SORT_PUT_INC((T[j] < c1) ? ~j : j);
			SA[i] = 0;
		} else if (j < 0) {
			SA[i] = ~j;
		}
	}
	SORT_BUCKET_FLUSH();
	if (C == B)
		FN(get_counts)(T, C, n, k);
	FN(get_buckets)(C, B, k, 1);
	c1 = 0;
	SORT_BUCKET_BEGIN();
	for (i = n - 1; 0 <= i; --i) {
		if (0 < (j = SA[i])) {
			SORT_BUCKET_SWITCH(c0 = T[j]);
			--j;
			SORT_PUT_DEC((T[j] > c1) ? ~(j + 1) : j);
			SA[i] = 0;
		}
	}
	SORT_BUCKET_FLUSH();
}

/*
 * Names the LMS substrings sorted by lms_sort1 by comparing neighbours.
 * Returns the number of names.
 */
static s32
FN(lms_postproc1)(const SYM *T, s32 *SA, s32 n, s32 m)
{
	s32 i, j, p, q, plen, qlen, name;
	s32 c0, c1;
	int diff;

	for (i = 0; (p = SA[i]) < 0; ++i)
		SA[i] = ~p;
	if (i < m) {
		for (j = i, ++i;; ++i) {
			if ((p = SA[i]) < 0) {
				SA[j++] = ~p;
				SA[i] = 0;
				if (j == m)
					break;
			}
		}
	}
	/*
	 * Length of each LMS substring (from its LMS position to the next one,
	 * inclusive) into SA[m + p/2], one position per iteration with the type
	 * recurrence.  A pair of positions 2k, 2k+1 holds at most one LMS
	 * position, so the slot is read back and rewritten unchanged for the
	 * other one.
	 */
	{
		s32 s1 = 0;
		j = n - 1;
		c1 = T[n - 1];
		for (i = n - 2; 0 <= i; --i) {
			c0 = T[i];
			s32 s0 = (c0 < c1) | ((c0 == c1) & s1);
			s32 lms = s1 & (s0 ^ 1);
			s32 *slot = SA + m + ((i + 1) >> 1);
			*slot = lms ? j - i : *slot;
			j = lms ? i + 1 : j;
			s1 = s0;
			c1 = c0;
		}
	}
	for (i = 0, name = 0, q = n, qlen = 0; i < m; ++i) {
		p = SA[i], plen = SA[m + (p >> 1)], diff = 1;
		if ((plen == qlen) && ((q + plen) < n)) {
			for (j = 0; (j < plen) && (T[p + j] == T[q + j]); ++j) {
			}
			if (j == plen)
				diff = 0;
		}
		if (diff != 0) {
			++name, q = p, qlen = plen;
		}
		SA[m + (p >> 1)] = name;
	}
	return name;
}

#endif

#if !SORT_TOP
/*
 * Final induction of a reduced problem: the full suffix array from the
 * sorted LMS suffixes.
 */
static void
FN(induce_sa)(const SYM *T, s32 *SA, s32 *C, s32 *B, s32 n, s32 k)
{
	s32 *b = NULL, i, j;
	s32 c0, c1;

	if (C == B)
		FN(get_counts)(T, C, n, k);
	FN(get_buckets)(C, B, k, 0);
	j = n - 1;
	c1 = T[j];
	SORT_BUCKET_BEGIN();
	SORT_PUT_INC(((0 < j) && (T[j - 1] < c1)) ? ~j : j);
	for (i = 0; i < n; ++i) {
		j = SA[i], SA[i] = ~j;
#  if SORT_SUFFIX0_APART
		/*
		 * Entry 1 induces suffix 0, which has no predecessor; every
		 * other inducing entry reads T[j - 1] unguarded.
		 */
		if (1 < j) {
			--j;
			SORT_BUCKET_SWITCH(c0 = T[j]);
			SORT_PUT_INC(SORT_MARK_IF(T[j - 1] < c1, j));
		} else if (j == 1) {
			SORT_BUCKET_SWITCH(c0 = T[0]);
			SORT_PUT_INC(0);
		}
#  else
		if (0 < j) {
			--j;
			SORT_BUCKET_SWITCH(c0 = T[j]);
			/* T[j - 1], or T[0] when j = 0 (then unused) */
			const s32 pv = T[j - (j > 0)];
			SORT_PUT_INC(SORT_MARK_IF((j > 0) & (pv < c1), j));
		}
#  endif
	}
	SORT_BUCKET_FLUSH();
	if (C == B)
		FN(get_counts)(T, C, n, k);
	FN(get_buckets)(C, B, k, 1);
	c1 = 0;
	SORT_BUCKET_BEGIN();
	for (i = n - 1; 0 <= i; --i) {
#  if SORT_SUFFIX0_APART
		if (1 < (j = SA[i])) {
			--j;
			SORT_BUCKET_SWITCH(c0 = T[j]);
			SORT_PUT_DEC(SORT_MARK_IF(T[j - 1] > c1, j));
		} else if (j == 1) {
			SORT_BUCKET_SWITCH(c0 = T[0]);
			SORT_PUT_DEC(~0);
		} else {
			SA[i] = ~j;
		}
#  else
		if (0 < (j = SA[i])) {
			--j;
			SORT_BUCKET_SWITCH(c0 = T[j]);
			const s32 pv = T[j - (j > 0)];
			SORT_PUT_DEC(SORT_MARK_IF((j == 0) | (pv > c1), j));
		} else {
			SA[i] = ~j;
		}
#  endif
	}
	SORT_BUCKET_FLUSH();
}
#endif

#if SORT_HAS_REDUCE
/*
 * Stages 1 and 2 of SA-IS, then the sorted LMS suffixes placed at the ends
 * of their buckets (stage 3 seeding); the caller runs the final induction
 * with the returned C and B (equal pointers mean the counts must be
 * recomputed there). SA has n + fs entries, and the bucket arrays come from
 * the last fs. Levels below the top recount their buckets after recursing
 * rather than reserving them, so each level's free space is at least its
 * parent's. *mode is the bucket-pointer mode for everything after stage 1:
 * chosen here at the top level from the L-scan's bucket changes, passed
 * down below it. freq (top level only, may be NULL) receives the symbol
 * counts. *seedp is where each bucket's seeds start, for the final L-scan,
 * or NULL (always NULL unless SORT_SKIP, and NULL when the free space cannot
 * hold it beside C and B). Returns 0, or -2 if a level's buckets do not fit.
 */
static s32
FN(sais_reduce)(const SYM *T, s32 *SA, s32 fs, s32 n, s32 k, s32 **Cp, s32 **Bp,
		s32 *freq, int *mode, s32 **seedp)
{
	s32 *C, *B, *D, *RA, *b;
	s32 i, j, m, p, t, name, newfs;
	s32 c0, c1;
	int recount, lmssort2;
	s32 *lms_list = NULL; /* LMS positions in ascending order, if listed */
	s32 used_tail = 0;    /* ints of bucket arrays at the end of SA */

	*seedp = NULL;
	if (2 * k <= fs) {
		C = SA + (n + fs - k);
		B = C - k;
		recount = 0;
	} else if (k <= fs) {
		C = B = SA + (n + fs - k);
		recount = 1;
	} else
		return -2;
	lmssort2 = !recount && (2 <= (n / k)) && (4 * k < fs);

	FN(get_counts)(T, C, n, k);
	if (freq != NULL)
		memcpy(freq, C, (size_t)k * sizeof(s32));
	FN(get_buckets)(C, B, k, 1);
	memset(SA, 0, (size_t)n * sizeof(s32));
	/*
	 * Each LMS position i+1 reserves the next slot from the end of bucket
	 * T[i+1]; the slot reserved by the previous (right-hand) LMS receives
	 * j, the L-type position before that LMS. Suffix types follow the
	 * recurrence s(i) = T[i] < T[i+1] or (T[i] = T[i+1] and s(i+1)), one
	 * position per iteration with selects instead of branches.
	 */
	{
		used_tail = lmssort2 ? 4 * k : recount ? k : 2 * k;
		if (fs - used_tail >= n / 2 + 1) {
			/*
			 * Two passes: the LMS positions into the free space
			 * just below the bucket arrays (found right to left,
			 * stored downwards, so they end up in ascending order),
			 * then the bucket slots for them, rightmost first.
			 */
			s32 *list_end = SA + n + fs - used_tail, s1 = 0;
			m = 0;
			c1 = T[n - 1];
			for (i = n - 2; 0 <= i; --i) {
				c0 = T[i];
				s32 s0 = (c0 < c1) | ((c0 == c1) & s1);
				list_end[-1 - m] = i + 1;
				m += s1 & (s0 ^ 1);
				s1 = s0;
				c1 = c0;
			}
			lms_list = list_end - m;
			b = &t;
			j = n;
			for (i = m - 1; 0 <= i; --i) {
				p = lms_list[i];
				*b = j;
				b = SA + --B[T[p]];
				j = p - 1;
			}
		} else {
			s32 dummy, s1 = 0;
			b = &t;
			j = n;
			m = 0;
			c1 = T[n - 1];
			for (i = n - 2; 0 <= i; --i) {
				c0 = T[i];
				s32 s0 = (c0 < c1) | ((c0 == c1) & s1);
				s32 lms = s1 & (s0 ^ 1);
				s32 *w = lms ? b : &dummy;
				*w = j;
				s32 nb = B[c1] - lms;
				B[c1] = nb;
				b = lms ? SA + nb : b;
				j = lms ? i : j;
				m += lms;
				s1 = s0;
				c1 = c0;
			}
		}
	}

	if (1 < m) {
		if (lmssort2) {
			D = B - k * 2;
			++B[T[j + 1]];
			for (i = 0, j = 0; i < k; ++i) {
				j += C[i];
				if (B[i] != j)
					SA[B[i]] += n;
				D[i] = D[i + k] = 0;
			}
			const s32 *s1_seed = NULL;
#  if SORT_SKIP
			/*
			 * B[c] is now where bucket c's seeds start; the L-scan
			 * gets a copy in the free space between SA[n) and the
			 * LMS list (or the bucket arrays) when it fits there.
			 */
			if ((lms_list != NULL ? lms_list : D) - (SA + n) >= k) {
				memcpy(SA + n, B, (size_t)k * sizeof(s32));
				s1_seed = SA + n;
			}
#  endif
			int repetitive;
			s32 d = FN(lms_sort2_l)(T, SA, C, B, D, n, k,
						&repetitive, s1_seed);
#  if SORT_TOP
			*mode = repetitive;
			(*mode ? lms_sort2_s_u8r : lms_sort2_s_u8m)(T, SA, C, B,
								    D, n, k, d);
#  else
			FN(lms_sort2_s)(T, SA, C, B, D, n, k, d);
#  endif
			name = FN(lms_postproc2)(SA, n, m);
		} else {
			FN(lms_sort1)(T, SA, C, B, n, k);
			name = FN(lms_postproc1)(T, SA, n, m);
		}
	} else if (m == 1) {
		*b = j + 1;
		name = 1;
	} else {
		name = 0;
	}

	/*
	 * Keep the LMS list through the recursion when the recursion still gets
	 * free space >= m (enough for every level below, see
	 * bzng_bwt_work_ints) and the reduced string, placed just below the
	 * list, stays clear of the names it is compacted from.  Otherwise the
	 * LMS positions are found again after the recursion.
	 */
	const int keep_list = lms_list != NULL &&
			      (s64)n + fs - used_tail >= 4 * (s64)m &&
			      (s64)n / 2 + fs - used_tail >= 3 * (s64)m;
	if (name < m) {
		if (keep_list) {
			/*
			 * [0, m + newfs) recursion | reduced string |
			 * LMS list | buckets
			 */
			newfs = n + fs - used_tail - 3 * m;
		} else {
			newfs = (n + fs) - (m * 2);
			if (!recount && SORT_TOP && (k + name) <= newfs)
				newfs -= k;
			else
				recount = 2;
		}
		RA = SA + m + newfs;
		/* Unconditional stores: a slot is rewritten until a name lands
		 * in it. */
		for (i = m + (n >> 1) - 1, j = m - 1; m <= i && 0 <= j; --i) {
			s32 v = SA[i];
			RA[j] = v - 1;
			j -= v != 0;
		}
		if (sort_main_i32(RA, SA, newfs, m, name, *mode) != 0)
			return -2;
		if (keep_list) {
			for (i = 0; i < m; ++i)
				SA[i] = lms_list[SA[i]];
		} else {
			/* RA[0..m) = the LMS positions, left to right. */
			s32 s1 = 0;
			j = m - 1;
			c1 = T[n - 1];
			for (i = n - 2; 0 <= i && 0 <= j; --i) {
				c0 = T[i];
				s32 s0 = (c0 < c1) | ((c0 == c1) & s1);
				RA[j] = i + 1;
				j -= s1 & (s0 ^ 1);
				s1 = s0;
				c1 = c0;
			}
			for (i = 0; i < m; ++i)
				SA[i] = RA[SA[i]];
		}
	}

	if (recount != 0)
		FN(get_counts)(T, C, n, k);
	if (1 < m) {
		s32 q2;
		FN(get_buckets)(C, B, k, 1);
#  if SORT_SKIP
		/*
		 * Where each bucket's seeds start (its end if it has none), for
		 * the final L-scan, in SA[n..n+k), which is free once the
		 * recursion is done.  It must lie below B, which also rules out
		 * C == B, where the final induction overwrites the counts with
		 * bucket heads that the skipping scan would read as sizes.
		 * bzng_bwt's work size always leaves the room; without it the
		 * final L-scan visits every slot.  The likely() hints matter to
		 * gcc, which inlines the whole top level into bzng_bwt; without
		 * them gcc 13's code there executes as many instructions but
		 * compresses text 0.7-0.8% slower on Zen 5.
		 */
		s32 *seed_start = NULL;
		if (likely(B - (SA + n) >= k)) {
			seed_start = SA + n;
			memcpy(seed_start, B, (size_t)k * sizeof(s32));
			*seedp = seed_start;
		}
#  endif
		i = m - 1, j = n, p = SA[m - 1], c1 = T[p];
		do {
			q2 = B[c0 = c1];
			while (q2 < j)
				SA[--j] = 0;
			do {
				SA[--j] = p;
				if (--i < 0)
					break;
				p = SA[i];
			} while ((c1 = T[p]) == c0);
#  if SORT_SKIP
			if (likely(seed_start != NULL))
				seed_start[c0] = j;
#  endif
		} while (0 <= i);
		while (0 < j)
			SA[--j] = 0;
	}
	*Cp = C;
	*Bp = B;
	return 0;
}

#endif

#if SORT_TOP
/*
 * Final induction for the block: leaves the BWT byte of every row in SA
 * (suffix 0's row holds 0, which the S-scan writes or, with
 * SORT_SUFFIX0_APART, finds left from the seeding; its index is returned) and
 * stores in *qrank the row of suffix q (1 <= q < n), recorded when that suffix
 * is written. Every suffix but 0 is written to its final row exactly once, so
 * this costs one compare per induced suffix. n >= 2. seed_start (may be NULL;
 * used only where SORT_SKIP) gives where each bucket's LMS seeds start.
 */
static s32
FN(compute_bwt)(const u8 *T, s32 *SA, s32 *C, s32 *B, s32 n, s32 q, s32 *qrank,
		const s32 *seed_start)
{
	s32 *b = NULL, i, j, pidx = -1, qr = -1;
	s32 c0, c1;
	(void)b;

	if (C == B)
		FN(get_counts)(T, C, n, 256);
	FN(get_buckets)(C, B, 256, 0);
	j = n - 1;
	c1 = T[j];
	SORT_BUCKET_BEGIN();
	qr = j == q ? SORT_POS_INC() : qr;
	SORT_PUT_INC(SORT_MARK_IF(T[j - 1] < c1, j));
	/*
	 * T is a Lyndon word, so suffix 0 is S-type and never induced in the
	 * L-scan: an inducing entry e is at least 2 and T[e - 2] needs no
	 * guard.
	 */
#  define SORT_BWT_L_VISIT(slot)                                   \
	  do {                                                     \
		  const s32 e_ = SA[slot];                         \
		  if (0 < e_) {                                    \
			  j = e_ - 1;                              \
			  c0 = T[j];                               \
			  SA[slot] = ~c0;                          \
			  SORT_BUCKET_SWITCH(c0);                  \
			  qr = j == q ? SORT_POS_INC() : qr;       \
			  const s32 pv_ = T[j - 1];                \
			  SORT_PUT_INC(SORT_MARK_IF(pv_ < c0, j)); \
		  } else if (e_ != 0) {                            \
			  SA[slot] = ~e_;                          \
		  }                                                \
	  } while (0)
#  if SORT_SKIP
	if (seed_start != NULL) {
		/*
		 * Each bucket's L part up to its live head, then its LMS
		 * seeds; the empty S slots between them are not visited.
		 */
		s32 start = 0;
		for (s32 c = 0; c < 256; ++c) {
			const s32 end = start + C[c];
			for (i = start; i < B[c]; ++i)
				SORT_BWT_L_VISIT(i);
			for (i = seed_start[c]; i < end; ++i)
				SORT_BWT_L_VISIT(i);
			start = end;
		}
	} else {
		for (i = 0; i < n; ++i)
			SORT_BWT_L_VISIT(i);
	}
#  else
	(void)seed_start;
	for (i = 0; i < n; ++i)
		SORT_BWT_L_VISIT(i);
#  endif
#  undef SORT_BWT_L_VISIT
	SORT_BUCKET_FLUSH();
	if (C == B)
		FN(get_counts)(T, C, n, 256);
	FN(get_buckets)(C, B, 256, 1);
	c1 = 0;
	SORT_BUCKET_BEGIN();
	for (i = n - 1; 0 <= i; --i) {
		const s32 e = SA[i];
#  if SORT_SUFFIX0_APART
		/*
		 * Only the entry of suffix 1 induces suffix 0, and it is left
		 * out: suffix 0 is the smallest suffix, so its row is 0, and
		 * SA[0] still holds the 0 the seeding left there.  Slot 0 is
		 * never a seed (suffix 0 is in bucket T[0] but is not LMS, so
		 * that bucket's seeds, placed from its end, stop short of it),
		 * the L-scan writes nothing to bucket T[0] (a Lyndon word of
		 * length >= 2 cannot end in its least symbol, so that bucket
		 * has no L-type suffix), and this scan fills the bucket from
		 * its end with every S-type suffix but 0.  That entry's row
		 * gets its BWT byte T[0]; every other inducing entry reads
		 * T[j - 1] unguarded.
		 */
		if (2 <= e) {
			j = e - 1;
			c0 = T[j];
			SA[i] = c0;
			SORT_BUCKET_SWITCH(c0);
			qr = j == q ? SORT_POS_DEC() : qr;
			const s32 pv = T[j - 1];
			SORT_PUT_DEC(SORT_SEL(pv > c0, ~pv, j));
		} else if (e < 0) {
			SA[i] = ~e;
		} else if (e == 0) {
			pidx = i;
		} else {
			SA[i] = T[0];
		}
#  else
		if (0 < e) {
			j = e - 1;
			c0 = T[j];
			SA[i] = c0;
			SORT_BUCKET_SWITCH(c0);
			qr = j == q ? SORT_POS_DEC() : qr;
			/* T[j - 1], or T[0] when j = 0 (then unused) */
			const s32 pv = T[j - (j > 0)];
			SORT_PUT_DEC(SORT_SEL((j > 0) & (pv > c0), ~pv, j));
		} else if (e != 0) {
			SA[i] = ~e;
		} else {
			pidx = i;
		}
#  endif
	}
	SORT_BUCKET_FLUSH();
	if (qr >= 0)
		*qrank = qr;
	return pidx;
}

#else
/*
 * Suffix array of the name string T[0..n) over [0, k) (one recursion level
 * and everything below it). Returns 0, or -2 if a level does not fit.
 */
static s32
FN(sort_main)(const s32 *T, s32 *SA, s32 fs, s32 n, s32 k)
{
	s32 *C, *B, *seed_start;
	int mode = SORT_REGISTER_BUCKET;
	if (FN(sais_reduce)(T, SA, fs, n, k, &C, &B, NULL, &mode,
			    &seed_start) != 0)
		return -2;
	FN(induce_sa)(T, SA, C, B, n, k);
	return 0;
}
#endif

#undef SORT_BUCKET_BEGIN
#undef SORT_BUCKET_SWITCH
#undef SORT_PUT_INC
#undef SORT_PUT_DEC
#undef SORT_POS_INC
#undef SORT_POS_DEC
#undef SORT_BUCKET_FLUSH
#undef SORT_HAS_REDUCE
#undef SORT_SKIP
#undef SORT_GROUP_START
#undef SORT_GROUP_FIRST
