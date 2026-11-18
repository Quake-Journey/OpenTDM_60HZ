/* mkdtemp for the API3 engine fixture on Windows (see win_compat.h). */
#ifdef _WIN32
#include <direct.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

/* engine.c sets `developer`; the release engine build (no USE_DEBUG) does
 * not define it. A debug engine build would report a duplicate at link. */
struct cvar_s;
struct cvar_s *developer;

/* The engine objects call MessageBoxA (Sys_Error, crash handler). The build
 * script redirects their import to this stub, so a harness error is a line
 * on stderr and an exit code, never a modal window on the user's desktop. */
static int WINAPI harness_MessageBoxA(HWND w, LPCSTR text, LPCSTR caption, UINT type)
{
    (void)w;
    fprintf(stderr, "harness: suppressed message box [%s] %s\n", caption ? caption : "",
            text ? text : "");
    fflush(stderr);
    return (type & 0xf) == MB_YESNO ? IDNO : IDOK;
}
int (WINAPI *__imp_harness_MessageBoxA)(HWND, LPCSTR, LPCSTR, UINT) = harness_MessageBoxA;

char *harness_mkdtemp(char *tmpl)
{
    size_t len = strlen(tmpl);
    if (len < 6 || strcmp(tmpl + len - 6, "XXXXXX"))
        return NULL;
    for (unsigned attempt = 0; attempt < 1000; attempt++) {
        unsigned v = (unsigned)GetCurrentProcessId() * 2654435761u ^ GetTickCount() ^ attempt * 40503u;
        static const char set[] = "abcdefghijklmnopqrstuvwxyz0123456789";
        for (int i = 0; i < 6; i++) {
            tmpl[len - 6 + i] = set[v % 36];
            v /= 36;
        }
        if (_mkdir(tmpl) == 0)
            return tmpl;
        if (errno != EEXIST)
            return NULL;
    }
    return NULL;
}
#endif
