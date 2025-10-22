#include <windows.h>

extern "C" void run_event_loop_impl() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

extern "C" void quit_event_loop_impl(int code) {
    PostQuitMessage(code);
}