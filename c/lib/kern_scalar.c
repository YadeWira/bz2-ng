/*
 * kern_scalar.c - the portable scalar kernel tier
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * No vector code (SWAR where it helps) and the table CRC.  The fallback for
 * any CPU and the reference every other tier is tested against.  No special
 * compiler flags.
 */

#define SUFFIX		_scalar
#define KERN_NO_VECTORS 1
#include "kern_template.h"
