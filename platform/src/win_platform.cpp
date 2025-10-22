#include <windows.h>
#include "lb_platform.h"

extern "C" {
struct lb_window;

lb_window *win_create_impl(int w, int h, const char *title_utf8);

void win_destroy_impl(lb_window *);

void win_present_rgba8_impl(lb_window *, const void *pixels, int w, int h, int stride);

void run_event_loop_impl();

void quit_event_loop_impl(int);
}

static LB_PlatformV1 g_v1{};

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
