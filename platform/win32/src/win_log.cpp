#include <windows.h>

#include <cstdarg>
#include <cstdio>

#include "lb_platform.h"

extern "C" void lbw_log(const char *fmt, ...) {
    if (!fmt) {
        return;
    }

    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (written < 0) {
        buffer[sizeof(buffer) - 1] = '\0';
    }

    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
}
