#include <windows.h>

#include "lb_platform.h"

extern "C" void lbw_register_event_thread(DWORD thread_id);
extern "C" void lbw_pump_posted_tasks();
extern "C" UINT lbw_post_task_msg();

extern "C" void run_event_loop_impl() {
    lbw_register_event_thread(GetCurrentThreadId());
    lbw_log("lb_platform: event loop running");

    MSG msg;
    const UINT POST_TASK = lbw_post_task_msg();

    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == POST_TASK) {
            lbw_pump_posted_tasks();
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    lbw_log("lb_platform: event loop exiting");
}

extern "C" void quit_event_loop_impl(int code) {
    PostQuitMessage(code);
}
