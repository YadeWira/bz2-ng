/*
 * sort_libsais_trap.c - the libsais block sorter with an aborting allocator
 *
 * lib/sort_libsais.c compiled again for a variant of the sorter test, with
 * the allocator libsais sees replaced by one that aborts. The library's own
 * build gives libsais a malloc that returns NULL, which would turn a block
 * that broke the free-space bound into an internal error; here it fails the
 * test. Linked into the test program, these definitions of bzng_bwt and
 * bzng_bwt_work_ints take the place of the library's.
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */
#include <stdio.h>
#include <stdlib.h>

/* Reports the allocation libsais attempted and aborts. */
static void *
trap_malloc(size_t size)
{
	fprintf(stderr,
		"sort_libsais_trap: libsais tried to allocate %zu bytes\n",
		size);
	abort();
}

#define BZNG_SAIS_MALLOC trap_malloc
/* With an allocator that never returns, libsais's code after it is dead. */
#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4702) /* unreachable code */
#endif
#include "sort_libsais.c"
#ifdef _MSC_VER
#  pragma warning(pop)
#endif
