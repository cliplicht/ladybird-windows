#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*lb_timer_cb)(void *);

typedef struct LB_PlatformV1 {
    int (*init)(void);

    void (*shutdown)(void);

    // Event loop
    void (*run_event_loop)(void);

    void (*quit_event_loop)(int code);

    // Async task posting (optional; maybe NULL if not supported)
    void (*post_task)(void (*fn)(void *), void *ctx);

    // Windowing / Present
    typedef struct lb_window lb_window;

    lb_window * (*win_create)(int w, int h, const char *title_utf8);

    void (*win_destroy)(lb_window *);

    void (*win_present_rgba8)(lb_window *, const void *pixels, int w, int h, int stride);

    void * (*timer_start)(unsigned ms, int repeat, lb_timer_cb cb, void *ctx);

    void (*timer_stop)(void *handle);
} LB_PlatformV1;

typedef int (*LB_QueryPlatformV1Fn)(LB_PlatformV1 *out); // exported by DLL


#ifdef __cplusplus
}
#endif
