#ifndef NAC_STRUTIL_H
#define NAC_STRUTIL_H

#include <stddef.h>

/* Case-insensitive (ASCII) comparison of two NUL-terminated strings. */
int nac_ci_equal(const char *a, const char *b);

/* Case-insensitive comparison of the first n bytes of a and b. */
int nac_ci_equal_n(const char *a, const char *b, size_t n);

/* Strips leading whitespace by returning a pointer into `text` and removes
 * trailing whitespace in place. */
char *nac_trim(char *text);

/* malloc-based strdup (portable across MSVC/GCC). NULL on failure. */
char *nac_strdup(const char *s);

#endif
