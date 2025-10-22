#include <windows.h>
#include <objbase.h>

#include "lb_platform.h"

extern "C" {
struct lb_window;

lb_window *win_create_impl(int w, int h, const char *title_utf8);
void win_destroy_impl(lb_window *);
LB_ErrorCode win_present_rgba8_impl(lb_window *, const void *pixels, int w, int h, int stride);
void win_set_event_callback_impl(lb_window *, LB_EventCallback, void *);

void run_event_loop_impl();
void quit_event_loop_impl(int);

void post_task_impl(void (*fn)(void *), void *ctx);
void lbw_register_event_thread(DWORD thread_id);

void *timer_start_impl(unsigned, int, void (*)(void *), void *);
void timer_stop_impl(void *);

LB_ErrorCode fs_read_entire_file_impl(const char *path_utf8, LB_FileResult *out);
LB_ErrorCode fs_write_entire_file_impl(const char *path_utf8, const void *data, size_t size);
LB_ErrorCode fs_remove_file_impl(const char *path_utf8);
LB_ErrorCode fs_stat_impl(const char *path_utf8, LB_FileStat *out);
LB_ErrorCode fs_list_directory_impl(const char *path_utf8, LB_DirectoryListing *out);
void lbw_buffer_free_impl(void *ptr);

LB_ErrorCode clipboard_write_text_impl(const char *utf8, size_t length);
LB_ErrorCode clipboard_read_text_impl(LB_Buffer *out);

LB_ErrorCode net_request_impl(const LB_NetRequestDesc *desc, LB_NetResponseCallback cb, void *ctx, lb_net_request **out_handle);
void net_request_cancel_impl(lb_net_request *handle);
}

static LB_PlatformV1 g_v1{};
static bool g_com_initialized = false;

static LB_ErrorCode platform_init() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        g_com_initialized = true;
    } else if (hr == RPC_E_CHANGED_MODE) {
        lbw_log("lb_platform: COM already initialized in different mode");
    } else {
        lbw_log("lb_platform: CoInitializeEx failed (hr=0x%08lx)", hr);
    }

    // Enable Per-Monitor v2 DPI awareness where available
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        using SetAwarenessCtx = BOOL(WINAPI *)(HANDLE);
        auto SetProcessDpiAwarenessContext =
            reinterpret_cast<SetAwarenessCtx>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (SetProcessDpiAwarenessContext) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = (HANDLE)-4
            SetProcessDpiAwarenessContext((HANDLE)-4);
        }
        FreeLibrary(user32);
    }

    lbw_register_event_thread(GetCurrentThreadId());
    lbw_log("lb_platform: init (thread=%lu)", static_cast<unsigned long>(GetCurrentThreadId()));
    return LB_Error_Ok;
}

extern "C" __declspec(dllexport)
LB_ErrorCode LB_QueryPlatformV1(LB_PlatformV1 *out) {
    if (!out) {
        return LB_Error_BadArgument;
    }

    g_v1.abi_version = LB_PLATFORM_ABI_VERSION;
    g_v1.init = platform_init;
    g_v1.shutdown = []() {
        if (g_com_initialized) {
            CoUninitialize();
            g_com_initialized = false;
        }
    };
    g_v1.run_event_loop = run_event_loop_impl;
    g_v1.quit_event_loop = quit_event_loop_impl;
    g_v1.post_task = post_task_impl;
    g_v1.timer_start = timer_start_impl;
    g_v1.timer_stop = timer_stop_impl;
    g_v1.win_create = win_create_impl;
    g_v1.win_destroy = win_destroy_impl;
    g_v1.win_present_rgba8 = win_present_rgba8_impl;
    g_v1.win_set_event_callback = win_set_event_callback_impl;
    g_v1.fs_read_entire_file = fs_read_entire_file_impl;
    g_v1.fs_write_entire_file = fs_write_entire_file_impl;
    g_v1.fs_remove_file = fs_remove_file_impl;
    g_v1.fs_stat = fs_stat_impl;
    g_v1.fs_list_directory = fs_list_directory_impl;
    g_v1.buffer_free = lbw_buffer_free_impl;
    g_v1.clipboard_write_text = clipboard_write_text_impl;
    g_v1.clipboard_read_text = clipboard_read_text_impl;
    g_v1.net_request = net_request_impl;
    g_v1.net_request_cancel = net_request_cancel_impl;

    *out = g_v1;
    lbw_log("lb_platform: ABI v%u exported", g_v1.abi_version);
    return LB_Error_Ok;
}
