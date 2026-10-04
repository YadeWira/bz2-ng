/*
 * sort.c - the block sorter
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
 * ALTERED for bz2-ng: the bzip2 block sorter (see sort.h).
 *
 * SA-IS as in sais-lite 2.4.1 (Nong, Zhang and Chan's induced sorting) on a
 * Lyndon word, of which the cyclic BWT and the row of one rotation are
 * wanted. Changes from sais-lite: no allocation (all scratch is the caller's
 * work array, sized by bzng_bwt_work_ints); the kernels are a template over
 * the symbol width and the bucket-pointer mode (sort_template.h); suffix
 * classification, LMS compaction and naming, the LMS-substring lengths and
 * the predecessor tests of the inductions select instead of branching; LMS
 * substrings are named during their induction; the list of LMS positions is
 * kept through the recursion when there is room, instead of classifying the
 * string again; the top level picks the bucket-pointer mode per block; the
 * final induction emits BWT bytes and records the row of rotation q, which
 * replaces a separate origPtr search; some scans take a form chosen per
 * compiler and target (below).  The licence is in
 * licenses/sais-lite-2.4.1.txt.
 */
#include "sort.h"

/*
 * Kernel forms chosen per compiler and target.  Each was measured on Apple
 * M4, Zen 5, Granite Rapids and Neoverse V2; the percentages are the sorter's
 * own throughput.  The output is the same either way, so the choice follows
 * the build, not the block.
 *
 * SORT_BRANCHLESS_NAMING: the stage-1 scans' name-group bookkeeping selects
 * and stores unconditionally.  7-13% faster on text on Apple's cores, up to
 * 10% slower on the three server cores, on text and repetitive data alike.
 *
 * gcc (SORT_GCC) compiles several of the scans' selects as branches where
 * clang emits conditional moves.  Under gcc the selects are therefore mask
 * arithmetic (SORT_MASK_SELECTS), and the top level's L-scans step over the
 * empty slots between each bucket's L part and its LMS seeds
 * (SORT_SKIP_EMPTY_SLOTS).  With SORT_SUFFIX0_APART these make gcc's sorter
 * 7-19% faster on text on Zen 5 and Granite Rapids (level-9 compression of
 * text 6.5-9.2% faster) and are neutral to 2% faster elsewhere; for clang
 * the masks and the skipping are within 3% either way.
 *
 * SORT_SUFFIX0_APART: the final scans that can induce suffix 0 take the entry
 * of suffix 1 on a separate path, so the common path reads the predecessor
 * without a bounds guard.  Neutral to 3% faster on the server cores, about
 * 1% slower on Apple's with clang.
 *
 * Each form can be overridden at compile time (-DSORT_...=0 or 1), so that
 * the tests can build every combination that some compiler and target ships.
 */
#if GCC_PREREQ(1, 0)
#  define SORT_GCC 1
#else
#  define SORT_GCC 0
#endif
#ifndef SORT_BRANCHLESS_NAMING
#  if defined(__APPLE__) && defined(ARCH_ARM64)
#    define SORT_BRANCHLESS_NAMING 1
#  else
#    define SORT_BRANCHLESS_NAMING 0
#  endif
#endif
#ifndef SORT_MASK_SELECTS
#  define SORT_MASK_SELECTS SORT_GCC
#endif
#ifndef SORT_SKIP_EMPTY_SLOTS
#  define SORT_SKIP_EMPTY_SLOTS SORT_GCC
#endif
#ifndef SORT_SUFFIX0_APART
#  if defined(__APPLE__) && defined(ARCH_ARM64) && !SORT_GCC
#    define SORT_SUFFIX0_APART 0
#  else
#    define SORT_SUFFIX0_APART 1
#  endif
#endif

/*
 * SORT_SEL(cond, a, b) is a when cond (0 or 1) is set, else b; SORT_MARK_IF
 * complements v when cond is set.
 */
#if SORT_MASK_SELECTS
#  define SORT_SEL(cond, a, b)	((b) ^ (((a) ^ (b)) & -(s32)(cond)))
#  define SORT_MARK_IF(cond, v) ((v) ^ -(s32)(cond))
#else
#  define SORT_SEL(cond, a, b)	((cond) ? (a) : (b))
#  define SORT_MARK_IF(cond, v) ((cond) ? ~(v) : (v))
#endif

static s32 sort_main_i32(const s32 *T, s32 *SA, s32 fs, s32 n, s32 k, int mode);
static void lms_sort2_s_u8m(const u8 *T, s32 *SA, const s32 *C, s32 *B, s32 *D,
			    s32 n, s32 k, s32 d);
static void lms_sort2_s_u8r(const u8 *T, s32 *SA, const s32 *C, s32 *B, s32 *D,
			    s32 n, s32 k, s32 d);

#define SYM		     u8
#define SORT_TOP	     1
#define FN(x)		     x##_u8m
#define SORT_REGISTER_BUCKET 0
#include "sort_template.h"
#undef FN
#undef SORT_REGISTER_BUCKET
#define FN(x)		     x##_u8r
#define SORT_REGISTER_BUCKET 1
#include "sort_template.h"
#undef FN
#undef SORT_REGISTER_BUCKET
#undef SYM
#undef SORT_TOP
#define SYM		     s32
#define SORT_TOP	     0
#define FN(x)		     x##_i32m
#define SORT_REGISTER_BUCKET 0
#include "sort_template.h"
#undef FN
#undef SORT_REGISTER_BUCKET
#define FN(x)		     x##_i32r
#define SORT_REGISTER_BUCKET 1
#include "sort_template.h"
#undef FN
#undef SORT_REGISTER_BUCKET
#undef SYM
#undef SORT_TOP

/*
 * Suffix array of the name string T[0..n) over [0, k), for the recursion, in
 * the bucket-pointer mode chosen at the top level.
 */
static s32
sort_main_i32(const s32 *T, s32 *SA, s32 fs, s32 n, s32 k, int mode)
{
	return mode ? sort_main_i32r(T, SA, fs, n, k) :
		      sort_main_i32m(T, SA, fs, n, k);
}

size_t
bzng_bwt_work_ints(s32 n)
{
	/*
	 * Every level below the top has alphabet < n/2 and at least the top
	 * level's free space minus its 256 counters, so n/2 + 1024 of free
	 * space always suffices. The other 1024 let the top level's list of LMS
	 * positions (at most n/2 + 1 slots) sit beside its 1024 bucket
	 * counters, which the faster two-pass classification needs.
	 */
	return (size_t)n + (size_t)n / 2 + 2048;
}

s32
bzng_bwt(const u8 *T, u8 *L, s32 n, s32 q, s32 *freq, s32 *work,
	 size_t work_ints)
{
	if (!T || !L || !work || n < 1 || q < 0 || q >= n)
		return -1;
	if (work_ints < bzng_bwt_work_ints(n))
		return -1;
	if (n == 1) {
		if (freq) {
			memset(freq, 0, 256 * sizeof(s32));
			freq[T[0]] = 1;
		}
		L[0] = T[0];
		return 0;
	}
	s32 *C, *B, *seed_start, qrank = -1;
	s32 fs = (s32)(work_ints - (size_t)n);
	if (fs > n + 2048)
		fs = n + 2048;
	int mode = 0;
	if (sais_reduce_u8m(T, work, fs, n, 256, &C, &B, freq, &mode,
			    &seed_start) != 0)
		return -2;
	s32 pidx =
		mode ? compute_bwt_u8r(T, work, C, B, n, q, &qrank,
				       seed_start) :
		       compute_bwt_u8m(T, work, C, B, n, q, &qrank, seed_start);
	/*
	 * T is a Lyndon word: suffix 0 is the smallest, and its row holds the
	 * cyclic predecessor T[n-1].
	 */
	if (pidx != 0)
		return -3;
	if (q == 0)
		qrank = 0;
	L[0] = T[n - 1];
	for (s32 i = 1; i < n; ++i)
		L[i] = (u8)work[i];
	return qrank;
}
