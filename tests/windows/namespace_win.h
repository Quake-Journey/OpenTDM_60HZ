/* Engine variant of the Windows API3 harness: tickrate-tests namespace.h
 * plus the declaration q_shared.h skips on MinGW once Q_strncasecmp is a
 * macro (its `#ifndef Q_strncasecmp` prototype guard). Linux maps the name
 * to libc strncasecmp instead, so upstream never needed this. */
#include "namespace.h"
#include <stddef.h>
int mod_Q_strncasecmp(const char *s1, const char *s2, size_t n);
