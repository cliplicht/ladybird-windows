#include <windows.h>
#include "lb_platform.h"

extern "C" {
struct lb_window;

lb_window *win_create_impl(int w, int h, const char *title_utf8);

void win_destroy_impl(lb_window *);

void win_present_rgba8_impl(lb_window *, const void *pixels, int w, int h, int stride);

void run_event_loop_impl();

void quit_event_loop_impl(int);

void post_task_impl(void (*fn)(void *), void *ctx);

void *timer_start_impl(unsigned, int, void (*)(void *), void *);

void timer_stop_impl(void *);
}

static LB_PlatformV1 g_v1{

    g_v1.init = []() -> int {
        // Enable Per-Monitor v2 DPI awareness where available
        HMODULE user32 = LoadLibraryW(L"user32.dll");
        if (user32) {
            using SetAwarenessCtx = BOOL (WINAPI*)(HANDLE);
            auto SetProcessDpiAwarenessContext =
                    reinterpret_cast<SetAwarenessCtx>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
            if (SetProcessDpiAwarenessContext) {
                // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = (HANDLE)-4
                SetProcessDpiAwarenessContext((HANDLE) -4);
            }
            FreeLibrary(user32);
        }

        g_v1.post_task = post_task_impl;
        g_v1.timer_start = timer_start_impl;
        g_v1.timer_stop  = timer_stop_impl;
        return 0;
    }


};

extern "C" __declspec(dllexport)
int LB_QueryPlatformV1(LB_PlatformV1 *out) {
    if (!out) return -1;
    g_v1.init = []() { return 0; };
    g_v1.shutdown = []() {
    };
    g_v1.run_event_loop = run_event_loop_impl;
    g_v1.quit_event_loop = quit_event_loop_impl;
    g_v1.win_create = win_create_impl;
    g_v1.win_destroy = win_destroy_impl;
    g_v1.win_present_rgba8 = win_present_rgba8_impl;
    *out = g_v1;
    return 0;
}
