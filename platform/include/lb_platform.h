#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct LB_PlatformV1 {
        int  (*init)(void);
        void (*shutdown)(void);

        void (*run_event_loop)(void);
        void (*quit_event_loop)(int code);

        typedef struct lb_window lb_window;
        lb_window* (*win_create)(int w, int h, const char* title_utf8);
        void       (*win_destroy)(lb_window*);
        void       (*win_present_rgba8)(lb_window*, const void* pixels, int w, int h, int stride);
    } LB_PlatformV1;

    typedef int (*LB_QueryPlatformV1Fn)(LB_PlatformV1* out); // exported by DLL

#ifdef __cplusplus
}
#endif
