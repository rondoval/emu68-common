// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _STRUTIL_H
#define _STRUTIL_H

#include <types.h>

LONG _Stricmp(CONST_STRPTR s1, CONST_STRPTR s2);
LONG _Strnicmp(CONST_STRPTR s1, CONST_STRPTR s2, LONG len);

/* Standard strncmp for third-party code in this -nostdlib tree; declared here
 * (compatibly with <string.h>) so the definition has a prototype. */
int strncmp(const char *s1, const char *s2, __SIZE_TYPE__ n);

/* Standard strlen and BSD strlcpy, same rationale. strlcpy copies at most
 * size-1 chars plus a NUL and returns strlen(src), so truncation shows as
 * return >= size. */
__SIZE_TYPE__ strlen(const char *s);
__SIZE_TYPE__ strlcpy(char *dst, const char *src, __SIZE_TYPE__ size);

#endif