/*
 * dec_block.c - decoder block finishing and text source
 *
 * Block finishing for the decoder: Huffman decoding tables, the per-stream
 * arrays, the inverse BWT (segmented table build, splitter marking and a
 * multi-chain walk into a pool of chunks, or libbz2's single chain in small
 * mode) and the text source the output stage reads.
 *
 * Notation: L is the BWT (the symbol loop's output), F the sorted column,
 * cftab the exclusive prefix sums of L's symbol counts. The table maps each
 * row to its successor in text order, T(LF(i)) = i; it is a permutation of
 * [0, n) because cftab is the exact prefix sum of the symbols written, so no
 * walk step needs a bounds check.
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */
#include <stdlib.h>
#include <string.h>

#include "dec.h"

/*
 * Builds the decoding table for one code-length set. The result decodes
 * every bit pattern exactly as libbz2's limit/base/perm tables do
 * (huffman.c BZ2_hbCreateDecodeTables with decompress.c GET_MTF_VAL): codes
 * are assigned canonically by length, then symbol; for an oversubscribed set
 * the codes that fall past the end of the code space are unreachable, and for
 * an incomplete set the unused patterns are no code.
 */
void
bzng_build_htable(bzng_htable *t, const u8 *length, s32 alpha_size)
{
	u32 count[BZNG_MAX_CODE + 2];
	memset(count, 0, sizeof count);
	for (s32 i = 0; i < alpha_size; i++)
		count[length[i]]++;
	int maxlen = 0;
	for (int l = 1; l <= BZNG_MAX_CODE; l++)
		if (count[l])
			maxlen = l;
	t->maxlen = (u8)maxlen;
	s32 offs[BZNG_MAX_CODE + 2];
	s32 code = 0, idx = 0;
	for (int l = 1; l <= BZNG_MAX_CODE; l++) {
		t->first_code[l] = code;
		t->first_idx[l] = idx;
		offs[l] = idx;
		code += (s32)count[l];
		idx += (s32)count[l];
		/*
		 * code <= 258 * 2^(l-1), so the shifted value stays below 2^28
		 */
		t->limit_lj[l] = (u32)code << (BZNG_MAX_CODE - l);
		code <<= 1;
	}
	t->limit_lj[BZNG_MAX_CODE + 1] = 0;
	for (s32 i = 0; i < alpha_size; i++)
		t->perm[offs[length[i]]++] = (u16)i;

	/*
	 * Codes of length <= K fill the primary table in canonical order; a
	 * code's slots start where the previous one's end, so stopping when the
	 * table is full is exactly libbz2's truncation of oversubscribed sets.
	 */
	const u32 size = 1u << BZNG_HUFF_K;
	u32 pos = 0;
	const int short_max = maxlen < BZNG_HUFF_K ? maxlen : BZNG_HUFF_K;
	for (int l = 1; l <= short_max && pos < size; l++) {
		const u32 span = 1u << (BZNG_HUFF_K - l);
		const u16 *sp = t->perm + t->first_idx[l];
		for (u32 k = 0; k < count[l] && pos < size; k++) {
			const u16 e = (u16)(sp[k] << 5 | l);
			for (u32 j = 0; j < span; j++)
				t->prim[pos + j] = e;
			pos += span;
		}
	}
	if (pos < size)
		memset(t->prim + pos, 0, (size - pos) * sizeof t->prim[0]);
}

/*
 * Per-quarter symbol counts of L[0..n): quarters [0,q), [q,2q), [2q,3q),
 * [3q,n) with q = n/4, counted in one interleaved pass so the four counter
 * chains run in parallel.
 */
static void
quarter_hist(const u8 *L, u32 n, u32 h[4][256])
{
	memset(h, 0, 4 * 256 * sizeof(u32));
	const u32 q = n / 4;
	for (u32 i = 0; i < q; i++) {
		h[0][L[i]]++;
		h[1][L[i + q]]++;
		h[2][L[i + 2 * q]]++;
		h[3][L[i + 3 * q]]++;
	}
	for (u32 i = 4 * q; i < n; i++)
		h[3][L[i]]++;
}

/*
 * Fills cftab (exclusive prefix sums, cftab[256] == n) and the per-quarter
 * scatter cursors from the quarter histograms.
 */
static void
cursors(bzng_dstate *s, u32 h[4][256], u32 cf[4][256])
{
	u32 sum = 0;
	for (int c = 0; c < 256; c++) {
		s->cftab[c] = sum;
		u32 at = sum;
		for (int k = 0; k < 4; k++) {
			cf[k][c] = at;
			at += h[k][c];
		}
		sum = at;
	}
	s->cftab[256] = sum;
}

/*
 * Row -> F symbol guess: fsym[q] is the F symbol of row q << shift; the true
 * symbol of a row is that or a later one (F is non-decreasing).
 */
static void
build_fsym(bzng_dstate *s, u32 n)
{
	u32 sh = 0;
	while (((n - 1) >> sh) >= BZNG_FSYM_MAX)
		sh++;
	s->fsym_shift = sh;
	const u32 entries = ((n - 1) >> sh) + 1;
	u32 c = 0;
	for (u32 q = 0; q < entries; q++) {
		const u32 r = q << sh;
		while (s->cftab[c + 1] <= r)
			c++;
		s->fsym[q] = (u8)c;
	}
}

/*
 * The F symbol of a row: the guess table's symbol for its group of rows, or
 * a later one (F is non-decreasing).
 */
static inline u8
row_symbol(const bzng_dstate *s, u32 row)
{
	u32 c = s->fsym[row >> s->fsym_shift];
	while (row >= s->cftab[c + 1])
		c++;
	return (u8)c;
}

#define PMASK 0x000FFFFFu

/*
 * Stores v (< 2^24) as entry j with one 4-byte store; byte 3j+3 (the next
 * entry's low byte, or padding) is overwritten with zero.
 */
static inline void
store3w(u8 *p3, u32 j, u32 v)
{
	memcpy(p3 + 3 * (size_t)j, &v, 4); /* little-endian */
}

/* Entry j of a 3-byte table (reads one pad byte past the end). */
static inline u32
load3(const u8 *p3, u32 j)
{
	u32 e;
	memcpy(&e, p3 + 3 * (size_t)j,
	       4); /* little-endian; one pad byte past the end */
	return e & 0x00FFFFFFu;
}

/*
 * Builds the T table from L (T(LF(i)) = i), four quarters with independent
 * cursors. fit: 3-byte entries holding the 20-bit pointer; otherwise 4-byte
 * entries i << 8 | L[i] (the symbol is F of the row being indexed).
 */
static void
build_t(bzng_dstate *s, u32 n, int fit)
{
	u32 h[4][256], cf[4][256];
	const u8 *L = s->L;
	quarter_hist(L, n, h);
	cursors(s, h, cf);
	const u32 q = n / 4;
	if (fit) {
		/*
		 * One 4-byte store per entry: it also zeroes the low byte of
		 * the next entry, which within a (quarter, symbol) cursor range
		 * is written later anyway; the first entry of each range is
		 * repaired below.
		 */
		u8 *p3 = s->tbl;
		u32 start[4][256];
		memcpy(start, cf, sizeof start);
		for (u32 i = 0; i < q; i++) {
			store3w(p3, cf[0][L[i]]++, i);
			store3w(p3, cf[1][L[i + q]]++, i + q);
			store3w(p3, cf[2][L[i + 2 * q]]++, i + 2 * q);
			store3w(p3, cf[3][L[i + 3 * q]]++, i + 3 * q);
		}
		for (u32 i = 4 * q; i < n; i++)
			store3w(p3, cf[3][L[i]]++, i);
		/*
		 * A range's first entry holds the first i of its quarter with
		 * that symbol; bits 8-19 survived, so the low byte is found by
		 * a scan of at most 256 symbols.
		 */
		for (int k = 0; k < 4; k++) {
			const u32 q_lo = (u32)k * q;
			for (int c = 0; c < 256; c++) {
				if (h[k][c] == 0)
					continue;
				u8 *e = p3 + 3 * (size_t)start[k][c];
				u32 i = ((u32)e[1] | (u32)e[2] << 8) << 8;
				if (i < q_lo)
					i = q_lo;
				while (L[i] != c)
					i++;
				e[0] = (u8)i;
			}
		}
	} else {
		u32 *tt = (u32 *)(void *)s->tbl;
		/*
		 * Each quarter has its own cursors, so all four cursors of an
		 * iteration are loaded before any entry is stored, and each
		 * entry is then stored before its cursor: with clang that order
		 * decodes repetitive data about 15% faster on Granite Rapids.
		 * The cursors never escape, so the compiler may reorder these
		 * accesses; clang keeps the order written here. gcc keeps the
		 * post-increment, which it compiles to tighter code that does
		 * not need the order.
		 */
		for (u32 i = 0; i < q; i++) {
			const u32 c0 = L[i], c1 = L[i + q], c2 = L[i + 2 * q],
				  c3 = L[i + 3 * q];
#if GCC_PREREQ(1, 0)
			tt[cf[0][c0]++] = i << 8 | c0;
			tt[cf[1][c1]++] = (i + q) << 8 | c1;
			tt[cf[2][c2]++] = (i + 2 * q) << 8 | c2;
			tt[cf[3][c3]++] = (i + 3 * q) << 8 | c3;
#else
			const u32 j0 = cf[0][c0], j1 = cf[1][c1],
				  j2 = cf[2][c2], j3 = cf[3][c3];

			tt[j0] = i << 8 | c0;
			cf[0][c0] = j0 + 1;
			tt[j1] = (i + q) << 8 | c1;
			cf[1][c1] = j1 + 1;
			tt[j2] = (i + 2 * q) << 8 | c2;
			cf[2][c2] = j2 + 1;
			tt[j3] = (i + 3 * q) << 8 | c3;
			cf[3][c3] = j3 + 1;
#endif
		}
		for (u32 i = 4 * q; i < n; i++) {
			const u32 c = L[i];
			tt[cf[3][c]++] = i << 8 | c;
		}
	}
}

/* Chains walked for the largest blocks; the pool's slack is sized for it. */
static u32
max_lanes(const bzng_dstate *s)
{
	return s->layout == BZNG_LAYOUT_WIDE ? BZNG_LANES_WIDE : BZNG_LANES_FIT;
}

/* Index of `row` in the sorted splitter list (row must be a splitter). */
static u32
seg_index(const bzng_dstate *s, u32 row)
{
	u32 lo = 0, hi = s->n_seg - 1;
	while (lo < hi) {
		const u32 mid = (lo + hi) >> 1;
		if (s->seg_row[mid] < row)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

/*
 * Chooses splitter rows (evenly spaced plus origPtr), marks them in the
 * table and returns the lane count.
 */
static u32
mark_splitters(bzng_dstate *s, u32 n, int fit)
{
	u32 want = n / BZNG_SEG_ROWS;
	if (want < 1)
		want = 1;
	if (want > s->seg_cap - 1)
		want = s->seg_cap - 1;
	const u32 op = (u32)s->orig_ptr;
	u32 m = 0;
	int inserted = 0;
	for (u32 k = 0; k < want; k++) {
		const u32 r = (u32)(((u64)k * n) / want);
		if (!inserted && op <= r) {
			if (m == 0 || s->seg_row[m - 1] != op)
				s->seg_row[m++] = op;
			inserted = 1;
		}
		if (m == 0 || s->seg_row[m - 1] != r)
			s->seg_row[m++] = r;
	}
	if (!inserted && (m == 0 || s->seg_row[m - 1] != op))
		s->seg_row[m++] = op;
	s->n_seg = m;
	for (u32 k = 0; k < m; k++) {
		if (fit)
			s->tbl[3 * (size_t)s->seg_row[k] + 2] |= 0x80;
		else
			((u32 *)(void *)s->tbl)[s->seg_row[k]] |= 0x80000000u;
	}
	s->seg_origin = seg_index(s, op);
	if (n >= 65536)
		return max_lanes(s);
	if (n >= 16384)
		return 8;
	if (m >= 2)
		return 2;
	return 1;
}

typedef struct {
	u32 p;	       /* current row */
	u8 *out, *end; /* write position and end of the current chunk */
	u32 written;   /* bytes written by this lane before the current chunk */
	u32 seg;       /* segment being walked */
	u32 mark;      /* lane byte count at the segment's start */
} walk_lane;

typedef struct {
	u8 *pool;
	u32 *chunk_next;
	u32 next_chunk;
	u32 next_split;
} walk_pool;

/* Bytes the lane has written in total. */
static inline u32
lane_count(const walk_lane *ln)
{
	return ln->written + (u32)(BZNG_CHUNK - (u32)(ln->end - ln->out));
}

/* Gives the lane a fresh pool chunk, linked after its current one. */
static inline void
new_chunk(walk_pool *w, walk_lane *ln)
{
	const u32 c = w->next_chunk++;
	if (ln->end) {
		const u32 prev = (u32)((ln->end - w->pool) / BZNG_CHUNK) - 1;
		w->chunk_next[prev] = c;
		ln->written += BZNG_CHUNK;
	}
	w->chunk_next[c] = 0;
	ln->out = w->pool + (size_t)c * BZNG_CHUNK;
	ln->end = ln->out + BZNG_CHUNK;
}

/*
 * Starts segment `seg` on a lane: records where its bytes begin, emits the
 * splitter row's own byte and moves to its successor.
 */
static inline void
seg_begin(bzng_dstate *s, walk_pool *w, walk_lane *ln, u32 seg, int fit)
{
	if (ln->out == ln->end)
		new_chunk(w, ln);
	s->seg_start[seg] = (u32)(ln->out - w->pool);
	ln->seg = seg;
	ln->mark = lane_count(ln);
	const u32 row = s->seg_row[seg];
	if (fit) {
		const u32 e = load3(s->tbl, row);
		*ln->out++ = row_symbol(s, row);
		ln->p = e & PMASK;
	} else {
		const u32 e = ((const u32 *)(const void *)s->tbl)[row];
		*ln->out++ = (u8)e;
		ln->p = (e >> 8) & PMASK;
	}
}

/* Records the length and successor segment of the lane's finished segment. */
static inline void
seg_end(bzng_dstate *s, walk_lane *ln)
{
	s->seg_len[ln->seg] = lane_count(ln) - ln->mark;
	s->seg_next[ln->seg] = seg_index(s, ln->p);
}

/*
 * One step of a lane: emits the current row's F byte and follows T, or ends
 * the segment at a marked row. Returns 0 when the lane has nothing left.
 * pf: prefetch the next row's table entry as soon as its index is known.
 */
static forceinline int
lane_step(bzng_dstate *s, walk_pool *w, walk_lane *ln, int fit, int pf)
{
	u32 e, next;
	u8 sym;
	if (fit) {
		e = load3(s->tbl, ln->p);
		if (unlikely(e & 0x800000u))
			goto seg_done;
		sym = row_symbol(s, ln->p);
		next = e & PMASK;
	} else {
		e = ((const u32 *)(const void *)s->tbl)[ln->p];
		if (unlikely(e & 0x80000000u))
			goto seg_done;
		sym = (u8)e;
		next = (e >> 8) & PMASK;
	}
	if (pf)
		prefetchr(s->tbl + (fit ? 3 : 4) * (size_t)next);
	if (unlikely(ln->out == ln->end))
		new_chunk(w, ln);
	*ln->out++ = sym;
	ln->p = next;
	return 1;
seg_done:
	seg_end(s, ln);
	if (w->next_split < s->n_seg) {
		seg_begin(s, w, ln, w->next_split++, fit);
		return 1;
	}
	return 0;
}

/*
 * Walks with at least this many lanes (blocks of at least 64 Ki rows)
 * prefetch; the tables of smaller blocks stay in cache.
 */
#define PREFETCH_MIN_LANES 16

/*
 * Walks every segment with K lanes in lock step (K fixed so the lane state
 * stays in registers), then finishes the stragglers. Every table is a
 * permutation of [0, n) because cftab is the exact prefix sum of the symbols
 * written, so no step needs a bounds check and every segment ends at a
 * marked row.
 */
#define DEFINE_WALK(K, FIT)                                                    \
	static void walk_##K##_##FIT(bzng_dstate *s, walk_pool *w)             \
	{                                                                      \
		const int pf = (K) >= PREFETCH_MIN_LANES;                      \
		walk_lane ln[K];                                               \
		u32 active = 0;                                                \
		while (active < (K) && w->next_split < s->n_seg) {             \
			ln[active].out = ln[active].end = NULL;                \
			ln[active].written = 0;                                \
			new_chunk(w, &ln[active]);                             \
			ln[active].written = 0;                                \
			seg_begin(s, w, &ln[active], w->next_split++, FIT);    \
			active++;                                              \
		}                                                              \
		if (active == (K)) {                                           \
			u32 dead = 0;                                          \
			while (!dead) {                                        \
				UNROLL_FULL for (int k = 0; k < (K);           \
						 k++) if (!lane_step(s, w,     \
								     &ln[k],   \
								     FIT, pf)) \
					dead |= 1u << k;                       \
			}                                                      \
			u32 a = 0;                                             \
			for (int k = 0; k < (K); k++)                          \
				if (!((dead >> k) & 1))                        \
					ln[a++] = ln[k];                       \
			active = a;                                            \
		}                                                              \
		while (active > 0) {                                           \
			for (u32 k = 0; k < active; k++) {                     \
				if (!lane_step(s, w, &ln[k], FIT, pf)) {       \
					ln[k] = ln[--active];                  \
					k--;                                   \
				}                                              \
			}                                                      \
		}                                                              \
	}

DEFINE_WALK(1, 0)
DEFINE_WALK(2, 0)
DEFINE_WALK(8, 0)
DEFINE_WALK(16, 0)
DEFINE_WALK(24, 0)
DEFINE_WALK(1, 1)
DEFINE_WALK(2, 1)
DEFINE_WALK(8, 1)
DEFINE_WALK(16, 1)

/* Walks every segment with the given number of lanes and table width. */
static void
walk(bzng_dstate *s, u32 lanes, int fit)
{
	walk_pool w = {s->L, s->chunk_next, 0, 0};
	switch (lanes) {
	case 24:
		/* only the wide layout, whose entries are all 4-byte */
		ASSERT(!fit);
		walk_24_0(s, &w);
		break;
	case 16:
		fit ? walk_16_1(s, &w) : walk_16_0(s, &w);
		break;
	case 8:
		fit ? walk_8_1(s, &w) : walk_8_0(s, &w);
		break;
	case 2:
		fit ? walk_2_1(s, &w) : walk_2_0(s, &w);
		break;
	default:
		fit ? walk_1_1(s, &w) : walk_1_0(s, &w);
		break;
	}
	/* The pool's slack covers one partly filled chunk per lane. */
	ASSERT(w.next_chunk <= s->chunk_cap);
}

/* libbz2's small-mode packed 20-bit entries (bzlib_private.h SET_LL/GET_LL). */
static inline u32
get_ll(const bzng_dstate *s, u32 i)
{
	const u16 *ll16 = (const u16 *)(const void *)s->tbl;
	return (u32)ll16[i] |
	       ((u32)((s->ll4[i >> 1] >> ((i << 2) & 0x4)) & 0xF) << 16);
}

/* Sets small-mode entry i to v (< 2^20) (libbz2's SET_LL). */
static inline void
set_ll(bzng_dstate *s, u32 i, u32 v)
{
	u16 *ll16 = (u16 *)(void *)s->tbl;
	ll16[i] = (u16)(v & 0xffff);
	const u32 hi = v >> 16;
	if ((i & 1) == 0)
		s->ll4[i >> 1] = (u8)((s->ll4[i >> 1] & 0xf0) | hi);
	else
		s->ll4[i >> 1] = (u8)((s->ll4[i >> 1] & 0x0f) | (hi << 4));
}

/*
 * Builds the inverse-BWT data for the block in s->L[0..n) and positions the
 * text reader at text position 0. No input or output is touched.
 */
void
bzng_finish_block(bzng_dstate *s)
{
	const u32 n = s->nblock;
	if (s->small) {
		/*
		 * L bytes sit at the start of the ll16 array. LF is computed in
		 * place from the end, so entry i (bytes 2i, 2i+1) only
		 * overwrites symbols already used; then libbz2's pointer
		 * reversal turns the cycle through origPtr into T.
		 */
		u32 h[4][256], cf[4][256], cend[256];
		quarter_hist(s->L, n, h);
		cursors(s, h, cf);
		for (int c = 0; c < 256; c++)
			cend[c] = s->cftab[c + 1];
		for (u32 i = n; i-- > 0;) {
			const u8 c = s->L[i];
			set_ll(s, i, --cend[c]);
		}
		u32 i = (u32)s->orig_ptr, j = get_ll(s, i);
		do {
			const u32 tmp = get_ll(s, j);
			set_ll(s, j, i);
			i = j;
			j = tmp;
		} while (i != (u32)s->orig_ptr);
		build_fsym(s, n);
		s->source = BZNG_SRC_SMALL;
		s->chain_pos = (u32)s->orig_ptr;
	} else {
		/*
		 * The fit layout reserves 3 bytes per row of the digit's block
		 * size for the table; a block of at most 3/4 of that size fits
		 * 4-byte entries there, which need no F-symbol lookup in the
		 * walk.
		 */
		const int fit = s->layout == BZNG_LAYOUT_FIT &&
				4 * (size_t)n > s->tbl_bytes;
		build_t(s, n, fit);
		if (fit)
			build_fsym(s, n);
		const u32 lanes = mark_splitters(s, n, fit);
		walk(s, lanes, fit);
		ASSERT(s->cftab[256] == n);
		s->source = BZNG_SRC_POOL;
		s->rd_seg = s->seg_origin;
		s->rd_off = s->seg_start[s->rd_seg];
		s->rd_seg_left = s->seg_len[s->rd_seg];
	}
	s->text_left = n;
	s->rd_len = 0;
	s->rd_ptr = NULL;
	s->r_n_to_go = 0;
	s->r_t_pos = 0;
	s->ur_eq = 0;
	s->ur_b = 0;
	s->pend_len = 0;
	s->pend_ch = 0;
	s->corrupt_pending = 0;
	s->calc_block_crc = 0xffffffffu;
}

/* ---- text source --------------------------------------------------------- */

/*
 * Next contiguous run of walk output from the pool, following segments in
 * text order (wrapping round the cycle as a forward walk would).
 */
static void
pool_piece(bzng_dstate *s, const u8 **ptr, u32 *len)
{
	if (s->rd_seg_left == 0) {
		s->rd_seg = s->seg_next[s->rd_seg];
		s->rd_off = s->seg_start[s->rd_seg];
		s->rd_seg_left = s->seg_len[s->rd_seg];
	}
	const u32 chunk = s->rd_off / BZNG_CHUNK;
	const u32 room = (chunk + 1) * BZNG_CHUNK - s->rd_off;
	const u32 l = room < s->rd_seg_left ? room : s->rd_seg_left;
	*ptr = s->L + s->rd_off;
	*len = l;
	s->rd_seg_left -= l;
	s->rd_off =
		(l == room) ? s->chunk_next[chunk] * BZNG_CHUNK : s->rd_off + l;
}

/* Next text symbol of the small-mode chain (before randomisation). */
static inline u8
chain_symbol(bzng_dstate *s)
{
	const u32 row = s->chain_pos;
	const u8 c = row_symbol(s, row);
	s->chain_pos = get_ll(s, row);
	return c;
}

/* libbz2's BZ_RAND_UPD_MASK / BZ_RAND_MASK for the next text position. */
static inline u8
rand_mask(bzng_dstate *s)
{
	if (s->r_n_to_go == 0) {
		s->r_n_to_go = bzng_rand_nums[s->r_t_pos];
		s->r_t_pos++;
		if (s->r_t_pos == 512)
			s->r_t_pos = 0;
	}
	s->r_n_to_go--;
	return s->r_n_to_go == 1 ? 1 : 0;
}

/*
 * Makes the next piece of text (at most `limit` bytes) current in
 * s->rd_ptr/s->rd_len. Pool blocks that are not randomised hand out the pool
 * directly; everything else goes through minibuf.
 */
void
bzng_next_piece(bzng_dstate *s, u32 limit)
{
	if (s->source == BZNG_SRC_POOL && !s->randomised) {
		const u8 *p;
		u32 l;
		pool_piece(s, &p, &l);
		if (l > limit) {
			/*
			 * keep the rest for a later piece: step the reader back
			 */
			const u32 back = l - limit;
			s->rd_seg_left += back;
			s->rd_off = (u32)(p - s->L) + limit;
			l = limit;
		}
		s->rd_ptr = p;
		s->rd_len = l;
		return;
	}
	u32 m = limit < BZNG_MINIBUF ? limit : BZNG_MINIBUF;
	u8 *mb = s->minibuf;
	if (s->source == BZNG_SRC_POOL) {
		/*
		 * randomised pool block: the pool reader state lives in rd_*
		 * too, so drain it byte-wise into minibuf through a private
		 * cursor
		 */
		for (u32 j = 0; j < m; j++) {
			if (s->rd_seg_left == 0 && s->rd_len == 0) {
				s->rd_seg = s->seg_next[s->rd_seg];
				s->rd_off = s->seg_start[s->rd_seg];
				s->rd_seg_left = s->seg_len[s->rd_seg];
			}
			const u32 chunk = s->rd_off / BZNG_CHUNK;
			const u8 c = s->L[s->rd_off];
			s->rd_seg_left--;
			s->rd_off++;
			if (s->rd_off == (chunk + 1) * BZNG_CHUNK &&
			    s->rd_seg_left)
				s->rd_off = s->chunk_next[chunk] * BZNG_CHUNK;
			mb[j] = c ^ rand_mask(s);
		}
	} else if (s->randomised) {
		for (u32 j = 0; j < m; j++)
			mb[j] = chain_symbol(s) ^ rand_mask(s);
	} else {
		for (u32 j = 0; j < m; j++)
			mb[j] = chain_symbol(s);
	}
	s->rd_ptr = mb;
	s->rd_len = m;
}

/*
 * Sets s->area_bytes (and s->area2_bytes in small mode) to what the
 * per-stream arrays of the header digit and layout need, with the sizes they
 * are carved into.  The driver allocates the areas (decompress.c), then
 * calls bzng_place_stream_arrays.
 */
void
bzng_size_stream_arrays(bzng_dstate *s)
{
	const u32 nmax = 100000u * (u32)s->block_size_100k;
	if (s->small) {
		s->area_bytes = 2 * (size_t)nmax + 64;
		s->area2_bytes = ((1 + (size_t)nmax) >> 1) + 8;
		return;
	}
	s->tbl_bytes = ((s->layout == BZNG_LAYOUT_FIT ? 3 : 4) * (size_t)nmax +
			8 + 15) &
		       ~(size_t)15;
	s->chunk_cap =
		(nmax + (max_lanes(s) + 1) * BZNG_CHUNK) / BZNG_CHUNK + 1;
	s->seg_cap = nmax / BZNG_SEG_ROWS + 2;
	s->area_bytes = s->tbl_bytes + (size_t)s->chunk_cap * BZNG_CHUNK +
			(size_t)s->chunk_cap * 4 + (size_t)s->seg_cap * 16;
	s->area2_bytes = 0;
}

/* Carves the allocated s->area (and s->area2) into the per-stream arrays. */
void
bzng_place_stream_arrays(bzng_dstate *s)
{
	if (s->small) {
		s->tbl = s->area;
		s->L = s->area;
		s->ll4 = s->area2;
		return;
	}
	const size_t pool_bytes = (size_t)s->chunk_cap * BZNG_CHUNK;
	s->tbl = s->area;
	s->L = s->area + s->tbl_bytes;
	s->chunk_next = (u32 *)(void *)(s->L + pool_bytes);
	s->seg_row = s->chunk_next + s->chunk_cap;
	s->seg_start = s->seg_row + s->seg_cap;
	s->seg_len = s->seg_start + s->seg_cap;
	s->seg_next = s->seg_len + s->seg_cap;
}
