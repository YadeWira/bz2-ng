/*
 * rand_hook.c - block randomisation for the test-only randomising compressor
 *
 * Derived from bzip2/libbzip2 1.0.8 (Copyright (C) 1996-2019 Julian Seward;
 * licence in reference/LICENSE): the mask is libbz2's BZ_RAND_MASK, which its
 * decoder applies to randomised blocks.  ALTERED for bz2-ng: written for the
 * tests, not part of libbz2.
 */

/*
 * Compiled with the reference headers and the rand_ prefix (see
 * test/CMakeLists.txt); called by the generated rand_compress.c before each
 * block is sorted.  bzip2 0.9.0 randomised blocks this way; its decoder XORs
 * the same mask back after the inverse BWT.
 */

#include "bzlib_private.h"

void bzng_test_randomise_block ( EState* s );

void bzng_test_randomise_block ( EState* s )
{
   Int32 i, rNToGo = 0, rTPos = 0;
   for (i = 0; i < s->nblock; i++) {
      if (rNToGo == 0) {
         rNToGo = BZ2_rNums[rTPos];
         rTPos++; if (rTPos == 512) rTPos = 0;
      }
      rNToGo--;
      if (rNToGo == 1) ((UChar*)s->block)[i] ^= 1;
   }
   for (i = 0; i < 256; i++) s->inUse[i] = False;
   for (i = 0; i < s->nblock; i++) s->inUse[((UChar*)s->block)[i]] = True;
}
