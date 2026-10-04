/*
 * enc_huff.c - the encoder's Huffman stage
 *
 * Huffman stage of the encoder: table selection, code lengths, and emission
 * of everything that follows the symbol map in a block (selectors,
 * code-length tables, coded MTF values).
 *
 * libbz2's algorithm (compress.c sendMTFValues): 2-6 tables by nMTF, the
 * equal-frequency seed partition, 4 iterations of "pick the cheapest table
 * per 50-symbol group (lowest index on ties), rebuild every table with
 * hb_make_code_lengths (maxLen 17)", canonical codes. The selectors
 * transmitted are those of the last iteration and the lengths those rebuilt
 * after it, so the output is libbz2's bit for bit.
 *
 * hb_make_code_lengths and hb_assign_codes are libbzip2 1.0.8's
 * BZ2_hbMakeCodeLengths and BZ2_hbAssignCodes (huffman.c), and seed_tables
 * and selector_mtf follow compress.c; libbzip2 is Copyright (C) 1996-2019
 * Julian Seward, under the licence in licenses/bzip2-1.0.8.txt. ALTERED for
 * bz2-ng: this is not the original software; names and types are changed and
 * the rest of the stage is rewritten.
 */
#include "enc.h"

#define LESSER_ICOST  0
#define GREATER_ICOST 15

/* ---- code lengths: libbz2 huffman.c, unchanged apart from names ---------- */
#define WEIGHTOF(zz0)	((zz0) & 0xffffff00)
#define DEPTHOF(zz1)	((zz1) & 0x000000ff)
#define MYMAX(zz2, zz3) ((zz2) > (zz3) ? (zz2) : (zz3))
#define ADDWEIGHTS(zw1, zw2)              \
	(WEIGHTOF(zw1) + WEIGHTOF(zw2)) | \
		(1 + MYMAX(DEPTHOF(zw1), DEPTHOF(zw2)))

#define UPHEAP(z)                                             \
	{                                                     \
		s32 zz, tmp;                                  \
		zz = z;                                       \
		tmp = heap[zz];                               \
		while (weight[tmp] < weight[heap[zz >> 1]]) { \
			heap[zz] = heap[zz >> 1];             \
			zz >>= 1;                             \
		}                                             \
		heap[zz] = tmp;                               \
	}

#define DOWNHEAP(z)                                                  \
	{                                                            \
		s32 zz, yy, tmp;                                     \
		zz = z;                                              \
		tmp = heap[zz];                                      \
		while (1) {                                          \
			yy = zz << 1;                                \
			if (yy > nHeap)                              \
				break;                               \
			if (yy < nHeap &&                            \
			    weight[heap[yy + 1]] < weight[heap[yy]]) \
				yy++;                                \
			if (weight[tmp] < weight[heap[yy]])          \
				break;                               \
			heap[zz] = heap[yy];                         \
			zz = yy;                                     \
		}                                                    \
		heap[zz] = tmp;                                      \
	}

/*
 * libbz2's BZ2_hbMakeCodeLengths: code lengths len[0..alphaSize) for the
 * frequencies freq, limited to maxLen bits by flattening the weights and
 * rebuilding until they fit.
 */
static void
hb_make_code_lengths(u8 *len, const s32 *freq, s32 alphaSize, s32 maxLen)
{
	s32 nNodes, nHeap, n1, n2, i, j, k;
	int tooLong;
	s32 heap[BZ_MAX_ALPHA_SIZE + 2];
	s32 weight[BZ_MAX_ALPHA_SIZE * 2];
	s32 parent[BZ_MAX_ALPHA_SIZE * 2];

	for (i = 0; i < alphaSize; i++)
		weight[i + 1] = (freq[i] == 0 ? 1 : freq[i]) << 8;

	while (1) {
		nNodes = alphaSize;
		nHeap = 0;
		heap[0] = 0;
		weight[0] = 0;
		parent[0] = -2;
		for (i = 1; i <= alphaSize; i++) {
			parent[i] = -1;
			nHeap++;
			heap[nHeap] = i;
			UPHEAP(nHeap);
		}
		while (nHeap > 1) {
			n1 = heap[1];
			heap[1] = heap[nHeap];
			nHeap--;
			DOWNHEAP(1);
			n2 = heap[1];
			heap[1] = heap[nHeap];
			nHeap--;
			DOWNHEAP(1);
			nNodes++;
			parent[n1] = parent[n2] = nNodes;
			weight[nNodes] = ADDWEIGHTS(weight[n1], weight[n2]);
			parent[nNodes] = -1;
			nHeap++;
			heap[nHeap] = nNodes;
			UPHEAP(nHeap);
		}
		tooLong = 0;
		for (i = 1; i <= alphaSize; i++) {
			j = 0;
			k = i;
			while (parent[k] >= 0) {
				k = parent[k];
				j++;
			}
			len[i - 1] = (u8)j;
			if (j > maxLen)
				tooLong = 1;
		}
		if (!tooLong)
			break;
		for (i = 1; i <= alphaSize; i++) {
			j = weight[i] >> 8;
			j = 1 + (j / 2);
			weight[i] = j << 8;
		}
	}
}

static void
hb_assign_codes(s32 *code, const u8 *length, s32 minLen, s32 maxLen,
		s32 alphaSize)
{
	s32 vec = 0;
	for (s32 n = minLen; n <= maxLen; n++) {
		for (s32 i = 0; i < alphaSize; i++)
			if (length[i] == n)
				code[i] = vec++;
		vec <<= 1;
	}
}

/* The number of coding tables for nmtf MTF values (compress.c:257-261). */
static s32
choose_n_groups(s32 nmtf)
{
	if (nmtf < 200)
		return 2;
	if (nmtf < 600)
		return 3;
	if (nmtf < 1200)
		return 4;
	if (nmtf < 2400)
		return 5;
	return 6;
}

/*
 * Initial tables: nGroups ranges of roughly equal total frequency, each table
 * cheap (0) inside its range and dear (15) outside (compress.c:279-317).
 */
static void
seed_tables(u8 len[BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE], s32 nGroups, s32 nmtf,
	    const s32 *mtf_freq, s32 alphaSize)
{
	for (s32 t = 0; t < BZ_N_GROUPS; t++)
		for (s32 v = 0; v < alphaSize; v++)
			len[t][v] = GREATER_ICOST;
	s32 nPart = nGroups, remF = nmtf, gs = 0;
	while (nPart > 0) {
		s32 tFreq = remF / nPart, ge = gs - 1, aFreq = 0;
		while (aFreq < tFreq && ge < alphaSize - 1) {
			ge++;
			aFreq += mtf_freq[ge];
		}
		if (ge > gs && nPart != nGroups && nPart != 1 &&
		    ((nGroups - nPart) % 2 == 1)) {
			aFreq -= mtf_freq[ge];
			ge--;
		}
		for (s32 v = 0; v < alphaSize; v++)
			len[nPart - 1][v] = (v >= gs && v <= ge) ?
						    LESSER_ICOST :
						    GREATER_ICOST;
		nPart--;
		gs = ge + 1;
		remF -= aFreq;
	}
}

/* Selector MTF (compress.c:461-478). */
static void
selector_mtf(const u8 *selector, s32 nSelectors, s32 nGroups, u8 *selectorMtf)
{
	u8 pos[BZ_N_GROUPS];
	for (s32 i = 0; i < nGroups; i++)
		pos[i] = (u8)i;
	for (s32 i = 0; i < nSelectors; i++) {
		u8 ll_i = selector[i], tmp = pos[0], tmp2;
		s32 j = 0;
		while (ll_i != tmp) {
			j++;
			tmp2 = tmp;
			tmp = pos[j];
			pos[j] = tmp2;
		}
		pos[0] = tmp;
		selectorMtf[i] = (u8)j;
	}
}

/* ---- the stage ----------------------------------------------------------- */

/*
 * libbz2's algorithm; differences are in how the work is done:
 * - all table costs of a symbol packed into one u64 (six 10-bit fields), so
 *   a group's costs for every table are one load + one add per symbol, for
 *   any table count and group length (a group costs <= 50 * 17 < 1024 per
 *   table);
 * - rfreq counted in two interleaved copies to break store-to-load chains on
 *   runs of the same MTF value;
 * - emission through a 64-bit accumulator with one (code << 5 | len) lookup
 *   per symbol and one flush per three symbols (7 + 3 * 17 <= 63 bits).
 */
u32
bzng_huff_encode(bzng_huff_scratch *h, const u16 *mtfv, s32 nMTF,
		 const s32 *mtf_freq, s32 alphaSize, bzng_bitwriter *bw)
{
	u8(*len)[BZ_MAX_ALPHA_SIZE] = h->len;
	s32(*rfreq)[BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE] = h->rfreq;
	u64 *pk = h->pk;
	u32(*ce)[BZ_MAX_ALPHA_SIZE] = h->ce;
	u8 *selector = h->selector, *selectorMtf = h->selector_mtf,
	   *prev_sel = h->prev_sel;
	s32 nSelectors = 0;

	s32 nGroups = choose_n_groups(nMTF);
	seed_tables(len, nGroups, nMTF, mtf_freq, alphaSize);
	s32 nFull = nMTF / BZ_G_SIZE, rem = nMTF % BZ_G_SIZE;

	for (s32 iter = 0; iter < BZ_N_ITERS; iter++) {
		/*
		 * After the first iteration rfreq[0] holds the
		 * per-table counts of the previous assignment, and only groups
		 * whose selector changed move their symbols between tables. The
		 * result is the same as recounting from zero, as libbz2 does.
		 */
		int recount = iter == 0;
		if (recount)
			memset(h->rfreq, 0, sizeof h->rfreq);
		for (s32 v = 0; v < alphaSize; v++) {
			u64 w = 0;
			for (s32 t = 0; t < nGroups; t++)
				w |= (u64)len[t][v] << (10 * t);
			pk[v] = w;
		}
		nSelectors = 0;
		const u16 *g = mtfv;
		for (s32 grp = 0; grp <= nFull; grp++, g += BZ_G_SIZE) {
			s32 gl = grp < nFull ? BZ_G_SIZE : rem;
			if (gl == 0)
				break;
			u64 s0 = 0, s1 = 0;
			if (gl == BZ_G_SIZE) {
				for (int k = 0; k < BZ_G_SIZE; k += 2) {
					s0 += pk[g[k]];
					s1 += pk[g[k + 1]];
				}
			} else {
				for (int k = 0; k < gl; k++)
					s0 += pk[g[k]];
			}
			u64 s = s0 + s1;
			u32 bc = (u32)(s & 1023), bt = 0;
			for (s32 t = 1; t < nGroups; t++) {
				u32 c = (u32)(s >> (10 * t)) & 1023;
				bt = c < bc ? (u32)t : bt;
				bc = c < bc ? c : bc;
			}
			selector[nSelectors++] = (u8)bt;
			if (recount) {
				s32 *ra = rfreq[0][bt], *rb = rfreq[1][bt];
				int k = 0;
				for (; k + 1 < gl; k += 2) {
					ra[g[k]]++;
					rb[g[k + 1]]++;
				}
				if (k < gl)
					ra[g[k]]++;
			} else if (bt != prev_sel[grp]) {
				s32 *from = rfreq[0][prev_sel[grp]],
				    *to = rfreq[0][bt];
				for (int k = 0; k < gl; k++) {
					from[g[k]]--;
					to[g[k]]++;
				}
			}
		}
		for (s32 t = 0; t < nGroups; t++) {
			if (recount)
				for (s32 v = 0; v < alphaSize; v++)
					rfreq[0][t][v] += rfreq[1][t][v];
			hb_make_code_lengths(&len[t][0], &rfreq[0][t][0],
					     alphaSize, BZNG_MAX_CODE_LEN);
		}
		memcpy(prev_sel, selector, (size_t)nSelectors);
	}

	selector_mtf(selector, nSelectors, nGroups, selectorMtf);
	s32 code[BZ_MAX_ALPHA_SIZE];
	for (s32 t = 0; t < nGroups; t++) {
		s32 minLen = 32, maxLen = 0;
		for (s32 i = 0; i < alphaSize; i++) {
			if (len[t][i] > maxLen)
				maxLen = len[t][i];
			if (len[t][i] < minLen)
				minLen = len[t][i];
		}
		hb_assign_codes(code, &len[t][0], minLen, maxLen, alphaSize);
		for (s32 i = 0; i < alphaSize; i++)
			ce[t][i] = ((u32)code[i] << 5) | len[t][i];
	}

	bzng_bw_put(bw, 3, (u64)nGroups);
	bzng_bw_put(bw, 15, (u64)nSelectors);
	for (s32 i = 0; i < nSelectors; i++) {
		u32 j = selectorMtf[i]; /* j ones then a zero */
		bzng_bw_put(bw, j + 1, ((1u << j) - 1) << 1);
	}
	for (s32 t = 0; t < nGroups; t++) {
		s32 curr = len[t][0];
		bzng_bw_put(bw, 5, (u64)curr);
		for (s32 i = 0; i < alphaSize; i++) {
			s32 target = len[t][i];
			while (curr < target) {
				bzng_bw_put(bw, 2, 2);
				curr++;
			}
			while (curr > target) {
				bzng_bw_put(bw, 2, 3);
				curr--;
			}
			bzng_bw_put(bw, 1, 0);
		}
	}

	/*
	 * Coded MTF values: three symbols per flush, which needs <= 7 bits
	 * pending on entry (bzng_bw_put flushes only when the accumulator is
	 * full).
	 */
	if (unlikely(bw->out > bw->end)) {
		bw->overflow = 1;
		bw->out = bw->end;
	}
	bzng_bw_flush(bw);
	u64 acc = bw->acc;
	u32 nbits = bw->nbits;
	u8 *out = bw->out;
	u8 *const end = bw->end - BZNG_GROUP_GUARD;

	STATIC_ASSERT(BZNG_GROUP_GUARD >=
		      (BZ_G_SIZE * BZNG_MAX_CODE_LEN + 7) / 8);
#define PUT(e) acc = (acc << ((e) & 31)) | ((e) >> 5), nbits += (e) & 31
#define FLUSH()                                               \
	do {                                                  \
		put_unaligned_be64(acc << (64 - nbits), out); \
		out += nbits >> 3;                            \
		nbits &= 7;                                   \
	} while (0)
	const u16 *g = mtfv;
	for (s32 grp = 0; grp < nSelectors; grp++, g += BZ_G_SIZE) {
		if (unlikely(out > end)) {
			bw->overflow = 1;
			break;
		}
		const u32 *tab = ce[selector[grp]];
		s32 gl = grp < nFull ? BZ_G_SIZE : rem;
		if (gl == BZ_G_SIZE) {
			for (int k = 0; k < 48; k += 3) {
				u32 e0 = tab[g[k]], e1 = tab[g[k + 1]],
				    e2 = tab[g[k + 2]];
				PUT(e0);
				PUT(e1);
				PUT(e2);
				FLUSH();
			}
			u32 e0 = tab[g[48]], e1 = tab[g[49]];
			PUT(e0);
			PUT(e1);
			FLUSH();
		} else {
			for (int k = 0; k < gl; k++) {
				u32 e = tab[g[k]];
				PUT(e);
				FLUSH();
			}
		}
	}
#undef PUT
#undef FLUSH
	bw->acc = acc;
	bw->nbits = nbits;
	bw->out = out;
	/* The end-of-block symbol is the last one coded. */
	return len[selector[nSelectors - 1]][alphaSize - 1];
}
