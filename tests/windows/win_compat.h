/* POSIX bits the tickrate-tests API3 engine fixture uses, for MinGW-w64.
 * Forced into engine.c only (-include); the module and the engine objects
 * are compiled exactly as for the delivered DLL / EXE. */
#ifndef HARNESS_WIN_COMPAT_H
#define HARNESS_WIN_COMPAT_H
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
char *harness_mkdtemp(char *tmpl);
#define mkdtemp(t) harness_mkdtemp(t)
#define mkdir(p, m) _mkdir(p)
#endif
#endif
