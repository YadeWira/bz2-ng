/*
 * Force-included into the stock libbzip2 1.0.8 sources in test/reference so
 * that several builds of them (the reference, and the randomising
 * compressor) link into one binary next to bz2-ng: every global symbol gets
 * the prefix BZPFX. Use: cc -DBZPFX=ref_ -include reference_rename.h ...
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */
#define BZR_CAT2(a, b)		   a##b
#define BZR_CAT(a, b)		   BZR_CAT2(a, b)
#define BZ2_blockSort		   BZR_CAT(BZPFX, BZ2_blockSort)
#define BZ2_bsInitWrite		   BZR_CAT(BZPFX, BZ2_bsInitWrite)
#define BZ2_bz__AssertH__fail	   BZR_CAT(BZPFX, BZ2_bz__AssertH__fail)
#define BZ2_bzBuffToBuffCompress   BZR_CAT(BZPFX, BZ2_bzBuffToBuffCompress)
#define BZ2_bzBuffToBuffDecompress BZR_CAT(BZPFX, BZ2_bzBuffToBuffDecompress)
#define BZ2_bzclose		   BZR_CAT(BZPFX, BZ2_bzclose)
#define BZ2_bzCompress		   BZR_CAT(BZPFX, BZ2_bzCompress)
#define BZ2_bzCompressEnd	   BZR_CAT(BZPFX, BZ2_bzCompressEnd)
#define BZ2_bzCompressInit	   BZR_CAT(BZPFX, BZ2_bzCompressInit)
#define BZ2_bzDecompress	   BZR_CAT(BZPFX, BZ2_bzDecompress)
#define BZ2_bzDecompressEnd	   BZR_CAT(BZPFX, BZ2_bzDecompressEnd)
#define BZ2_bzDecompressInit	   BZR_CAT(BZPFX, BZ2_bzDecompressInit)
#define BZ2_bzdopen		   BZR_CAT(BZPFX, BZ2_bzdopen)
#define BZ2_bzerror		   BZR_CAT(BZPFX, BZ2_bzerror)
#define BZ2_bzflush		   BZR_CAT(BZPFX, BZ2_bzflush)
#define BZ2_bzlibVersion	   BZR_CAT(BZPFX, BZ2_bzlibVersion)
#define BZ2_bzopen		   BZR_CAT(BZPFX, BZ2_bzopen)
#define BZ2_bzread		   BZR_CAT(BZPFX, BZ2_bzread)
#define BZ2_bzRead		   BZR_CAT(BZPFX, BZ2_bzRead)
#define BZ2_bzReadClose		   BZR_CAT(BZPFX, BZ2_bzReadClose)
#define BZ2_bzReadGetUnused	   BZR_CAT(BZPFX, BZ2_bzReadGetUnused)
#define BZ2_bzReadOpen		   BZR_CAT(BZPFX, BZ2_bzReadOpen)
#define BZ2_bzwrite		   BZR_CAT(BZPFX, BZ2_bzwrite)
#define BZ2_bzWrite		   BZR_CAT(BZPFX, BZ2_bzWrite)
#define BZ2_bzWriteClose	   BZR_CAT(BZPFX, BZ2_bzWriteClose)
#define BZ2_bzWriteClose64	   BZR_CAT(BZPFX, BZ2_bzWriteClose64)
#define BZ2_bzWriteOpen		   BZR_CAT(BZPFX, BZ2_bzWriteOpen)
#define BZ2_compressBlock	   BZR_CAT(BZPFX, BZ2_compressBlock)
#define BZ2_crc32Table		   BZR_CAT(BZPFX, BZ2_crc32Table)
#define BZ2_decompress		   BZR_CAT(BZPFX, BZ2_decompress)
#define BZ2_hbAssignCodes	   BZR_CAT(BZPFX, BZ2_hbAssignCodes)
#define BZ2_hbCreateDecodeTables   BZR_CAT(BZPFX, BZ2_hbCreateDecodeTables)
#define BZ2_hbMakeCodeLengths	   BZR_CAT(BZPFX, BZ2_hbMakeCodeLengths)
#define BZ2_indexIntoF		   BZR_CAT(BZPFX, BZ2_indexIntoF)
#define BZ2_rNums		   BZR_CAT(BZPFX, BZ2_rNums)
