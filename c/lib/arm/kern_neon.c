/*
 * arm/kern_neon.c - the AArch64 NEON kernel tier
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * NEON is baseline on AArch64, so this needs no special compiler flags; the
 * tier's CRC kernel (the CRC32 instructions) is arm/crc32_armv8.c.  Compiles
 * to nothing on other architectures.
 */

#include "../lib_common.h"

#ifdef ARCH_ARM64
#  define SUFFIX _neon
#  include "../kern_template.h"
#else
typedef int bzng_kern_neon_unused; /* ISO C forbids an empty translation unit */
#endif
