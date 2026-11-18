/* POSIX signal-jump names the API3 harness scenarios use, for MinGW-w64.
 * Forced into harness.c only (-include); module objects are not affected. */
#ifndef HARNESS_COMPAT_H
#define HARNESS_COMPAT_H
#ifdef _WIN32
#include <setjmp.h>
#include <signal.h>
#define sigjmp_buf jmp_buf
#define sigsetjmp(b, s) setjmp(b)
#define siglongjmp(b, v) longjmp(b, v)
#endif
#endif
