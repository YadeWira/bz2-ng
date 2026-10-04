/*
 * test_tier.h - which kernel tier a test runs on
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

#ifndef TEST_TEST_TIER_H
#define TEST_TEST_TIER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kern.h"

/*
 * Checks BZ2NG_TIER against the tier the library selected.  A name that is
 * no tier at all is an error (exit 1), so that a misspelling cannot pass as
 * a missing CPU feature; a real tier the CPU lacks makes the test report
 * itself skipped (exit 77, CTest's SKIP_RETURN_CODE).
 */
static inline void
require_requested_tier(void)
{
	static const char *const known[] = {"scalar", "sse4", "avx2", "neon"};
	const char *want = getenv("BZ2NG_TIER");
	const char *got = bzng_tier_name();
	size_t i;

	if (want == NULL || *want == '\0')
		return;
	for (i = 0; i < sizeof(known) / sizeof(known[0]); i++)
		if (strcmp(want, known[i]) == 0)
			break;
	if (i == sizeof(known) / sizeof(known[0])) {
		printf("BZ2NG_TIER=%s is not a kernel tier (scalar, sse4, "
		       "avx2, neon)\n",
		       want);
		exit(1);
	}
	if (strcmp(want, got) != 0) {
		printf("tier %s not available on this CPU (selected %s): "
		       "skipped\n",
		       want, got);
		exit(77);
	}
}

#endif /* TEST_TEST_TIER_H */
