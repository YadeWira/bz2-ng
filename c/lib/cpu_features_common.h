/*
 * cpu_features_common.h - code shared by all lib/$arch/cpu_features.c
 *
 * Copyright 2020 Eric Biggers
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
 * Taken from libdeflate 1.26 (lib/cpu_features_common.h).  Changes for bz2-ng:
 * the test hook is BZ2NG_TIER, which names a kernel tier rather than features
 * to disable, is always compiled in, and ignores names it does not know
 * instead of aborting.
 */

#ifndef LIB_CPU_FEATURES_COMMON_H
#define LIB_CPU_FEATURES_COMMON_H

#include <stdlib.h>
#include <string.h>

#include "lib_common.h"

/* A kernel tier and the CPU features it may use. */
struct tier_features {
	const char *name;
	u32 allowed;
};

/*
 * Restrict *features to the tier named in $BZ2NG_TIER, for tests and
 * measurement.  This can only remove features, so naming a tier the CPU does
 * not support selects the best tier it does support below that one; an
 * unknown name changes nothing.
 */
static inline void
apply_tier_override(u32 *features, const struct tier_features *tiers,
		    size_t num_tiers)
{
	const char *name = getenv("BZ2NG_TIER");
	size_t i;

	if (name == NULL)
		return;
	for (i = 0; i < num_tiers; i++) {
		if (strcmp(name, tiers[i].name) == 0) {
			*features &= tiers[i].allowed;
			return;
		}
	}
}

#endif /* LIB_CPU_FEATURES_COMMON_H */
