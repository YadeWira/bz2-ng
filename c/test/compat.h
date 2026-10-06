/*
 * compat.h - the POSIX functions the tests and the benchmark use
 *
 * Part of bz2-ng; MIT licence (see LICENSE).
 */

/*
 * getopt, mkstemp, close, unlink and strtok_r come from <unistd.h>,
 * <stdlib.h> and <string.h> wherever there is a POSIX C library, mingw-w64's
 * included. Microsoft's C runtime has close and unlink in <io.h> and
 * strtok_r as strtok_s; getopt and mkstemp are written here.
 */
#ifndef TEST_COMPAT_H
#define TEST_COMPAT_H

#ifndef _MSC_VER
#  include <unistd.h>
#else
#  include <errno.h>
#  include <fcntl.h>
#  include <io.h>
#  include <process.h>
#  include <stdio.h>
#  include <string.h>
#  include <sys/stat.h>

#  define strtok_r strtok_s

static char *optarg;
static int optind = 1;

/*
 * POSIX getopt without its error-reporting variables: options come before
 * the first operand, flags may be grouped ("-qv"), an option's argument is
 * the rest of its word or the next word, and "--" ends the options.  Returns
 * '?' for an unknown option or a missing argument, after a message on stderr.
 */
static inline int
getopt(int argc, char *const argv[], const char *optstring)
{
	static int pos = 1; /* the next flag's index in argv[optind] */
	const char *word, *spec;
	int c;

	if (pos == 1) {
		if (optind >= argc || argv[optind][0] != '-' ||
		    argv[optind][1] == '\0')
			return -1;
		if (strcmp(argv[optind], "--") == 0) {
			optind++;
			return -1;
		}
	}
	word = argv[optind];
	c = (unsigned char)word[pos++];
	spec = c == ':' ? NULL : strchr(optstring, c);
	if (spec != NULL && spec[1] == ':') {
		if (word[pos] != '\0')
			optarg = (char *)&word[pos];
		else if (optind + 1 < argc)
			optarg = argv[++optind];
		else
			spec = NULL;
		pos = (int)strlen(word);
	}
	if (word[pos] == '\0') {
		optind++;
		pos = 1;
	}
	if (spec == NULL) {
		fprintf(stderr, "%s: bad option or missing argument: -%c\n",
			argv[0], c);
		return '?';
	}
	return c;
}

/*
 * POSIX mkstemp: replaces the trailing XXXXXX of name to create a new file
 * exclusively, opened for reading and writing in binary mode; returns its
 * descriptor, or -1.
 */
static inline int
mkstemp(char *name)
{
	char *x = name + strlen(name) - 6;

	for (unsigned attempt = 0; attempt < 4096; attempt++) {
		int fd;

		snprintf(x, 7, "%03x%03x", (unsigned)_getpid() & 0xfff,
			 attempt);
		fd = _open(name, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
			   _S_IREAD | _S_IWRITE);
		if (fd >= 0 || errno != EEXIST)
			return fd;
	}
	return -1;
}
#endif /* _MSC_VER */

#endif /* TEST_COMPAT_H */
