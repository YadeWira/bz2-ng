/*
 * arm/cpu_features.c - feature detection for ARM CPUs
 *
 * Copyright 2016 Eric Biggers
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
 * Adapted from libdeflate 1.26 (lib/arm/cpu_features.c) for bz2-ng: AArch64
 * on Linux and Apple platforms, the features NEON, CRC32 and PMULL, and the
 * BZ2NG_TIER and BZ2NG_NO_PMULL overrides.
 */

/*
 * ARM CPUs don't have a standard way for unprivileged programs to detect CPU
 * features.  But an OS-specific way can be used when available.
 */

#ifdef __APPLE__
#  undef _ANSI_SOURCE
#  undef _DARWIN_C_SOURCE
#  define _DARWIN_C_SOURCE /* for sysctlbyname() */
#endif

#include "../cpu_features_common.h" /* must be included first */
#include "cpu_features.h"

#include <errno.h>

#ifdef ARM_CPU_FEATURES_KNOWN
/* Runtime ARM CPU feature detection is supported. */

#ifdef __linux__
/*
 * On Linux, arm64 CPU features can be detected by reading the AT_HWCAP and
 * AT_HWCAP2 values from /proc/self/auxv.
 *
 * Ideally we'd use the C library function getauxval(), but it's not guaranteed
 * to be available: it was only added to glibc in 2.16, and in Android it was
 * added to API level 18 for arm32 and level 21 for arm64.
 */

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define AT_HWCAP	16
#define AT_HWCAP2	26

static void scan_auxv(unsigned long *hwcap, unsigned long *hwcap2)
{
	int fd;
	unsigned long auxbuf[32];
	int filled = 0;
	int i;

	fd = open("/proc/self/auxv", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return;

	for (;;) {
		do {
			int ret = read(fd, &((char *)auxbuf)[filled],
				       sizeof(auxbuf) - filled);
			if (ret <= 0) {
				if (ret < 0 && errno == EINTR)
					continue;
				goto out;
			}
			filled += ret;
		} while (filled < (int)(2 * sizeof(long)));

		i = 0;
		do {
			unsigned long type = auxbuf[i];
			unsigned long value = auxbuf[i + 1];

			if (type == AT_HWCAP)
				*hwcap = value;
			else if (type == AT_HWCAP2)
				*hwcap2 = value;
			i += 2;
			filled -= 2 * sizeof(long);
		} while (filled >= (int)(2 * sizeof(long)));

		memmove(auxbuf, &auxbuf[i], filled);
	}
out:
	close(fd);
}

static u32 query_arm_cpu_features(void)
{
	u32 features = 0;
	unsigned long hwcap = 0;
	unsigned long hwcap2 = 0;

	scan_auxv(&hwcap, &hwcap2);

	STATIC_ASSERT(sizeof(long) == 8);
	if (hwcap & (1 << 1))	/* HWCAP_ASIMD */
		features |= ARM_CPU_FEATURE_NEON;
	if (hwcap & (1 << 4))	/* HWCAP_PMULL */
		features |= ARM_CPU_FEATURE_PMULL;
	if (hwcap & (1 << 7))	/* HWCAP_CRC32 */
		features |= ARM_CPU_FEATURE_CRC32;
	return features;
}

#elif defined(__APPLE__)
/* On Apple platforms, arm64 CPU features can be detected via sysctlbyname(). */

#include <sys/types.h>
#include <sys/sysctl.h>

static const struct {
	const char *name;
	u32 feature;
} feature_sysctls[] = {
	{ "hw.optional.neon",		  ARM_CPU_FEATURE_NEON },
	{ "hw.optional.AdvSIMD",	  ARM_CPU_FEATURE_NEON },
	{ "hw.optional.arm.FEAT_PMULL",	  ARM_CPU_FEATURE_PMULL },
	{ "hw.optional.armv8_crc32",	  ARM_CPU_FEATURE_CRC32 },
};

static u32 query_arm_cpu_features(void)
{
	u32 features = 0;
	size_t i;

	for (i = 0; i < ARRAY_LEN(feature_sysctls); i++) {
		const char *name = feature_sysctls[i].name;
		u32 val = 0;
		size_t valsize = sizeof(val);

		if (sysctlbyname(name, &val, &valsize, NULL, 0) == 0 &&
		    valsize == sizeof(val) && val == 1)
			features |= feature_sysctls[i].feature;
	}
	return features;
}
#else
#error "unhandled case"
#endif

/* The tiers BZ2NG_TIER can name, and the features each may use. */
static const struct tier_features arm_tiers[] = {
	{"scalar",	0},
	{"neon",	~0U},
};

volatile u32 bzng_arm_cpu_features = 0;

/*
 * Initialize bzng_arm_cpu_features.  errno is preserved: the queries may fail
 * and set it, and programs such as bzip2 report errno after a library call
 * returns an error.  A non-empty $BZ2NG_NO_PMULL removes PMULL, so that tests
 * can run the NEON tier as on a core without it.
 */
void bzng_init_arm_cpu_features(void)
{
	int saved_errno = errno;
	u32 features = query_arm_cpu_features();
	const char *no_pmull = getenv("BZ2NG_NO_PMULL");

	apply_tier_override(&features, arm_tiers, ARRAY_LEN(arm_tiers));
	if (no_pmull != NULL && *no_pmull != '\0')
		features &= ~ARM_CPU_FEATURE_PMULL;

	bzng_arm_cpu_features = features | ARM_CPU_FEATURES_KNOWN;
	errno = saved_errno;
}

#else
typedef int bzng_arm_cpu_features_unused; /* ISO C forbids an empty TU */
#endif /* ARM_CPU_FEATURES_KNOWN */
