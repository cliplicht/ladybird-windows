#include <windows.h>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include "lb_platform.h"

static void debug_log(const char* fmt, ...) {
    if (!fmt) return;
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
}

// Small state bucket for the animation timer callback
struct AnimState {
    LB_PlatformV1 plat{};
    lb_window* win{};
    int W{800}, H{600}, Stride{0};
    uint32_t tick{0};
    // Static buffer for simplicity; for real code prefer a heap/vector
    unsigned char* pixels{nullptr};
};

static void handle_platform_event(const LB_Event* event, void* ctx) {
    if (!event || !ctx) {
        return;
    }
    auto* state = static_cast<AnimState*>(ctx);
    switch (event->type) {
        case LB_Event_WindowClose:
            if (state->plat.quit_event_loop) {
                state->plat.quit_event_loop(0);
            }
            break;
        case LB_Event_KeyDown:
            if (event->data.key.virtual_key == VK_ESCAPE && state->plat.quit_event_loop) {
                state->plat.quit_event_loop(0);
            }
            break;
        default:
            break;
    }
}

// Procedural pattern into RGBA8 buffer
static void fill_pattern(AnimState* s) {
    if (!s || !s->pixels) return;
    unsigned char* p = s->pixels;
    const int stride = s->Stride;
    const uint32_t t = s->tick;

    for (int y = 0; y < s->H; ++y) {
        for (int x = 0; x < s->W; ++x) {
            const int i = y * stride + x * 4;
            const uint8_t r = static_cast<uint8_t>((x + (t >> 1)) & 0xFF);
            const uint8_t g = static_cast<uint8_t>((y * 2 + (t >> 0)) & 0xFF);
            const uint8_t b = static_cast<uint8_t>(((x ^ y) + (t >> 2)) & 0xFF);
            p[i + 0] = b;   // B
            p[i + 1] = g;   // G
            p[i + 2] = r;   // R
            p[i + 3] = 0xFF;// A (unused by GDI)
        }
    }
}

// Runs on the UI thread via post_task; presents the current buffer
static void ui_present(void* ctx) {
    auto* s = static_cast<AnimState*>(ctx);
    if (!s || !s->win) return;
    if (s->plat.win_present_rgba8) {
        LB_ErrorCode rc = s->plat.win_present_rgba8(s->win, s->pixels, s->W, s->H, s->Stride);
        if (rc != LB_Error_Ok) {
            debug_log("lbw_bootstrap: win_present_rgba8 failed (%d)", rc);
        }
    }
}

// Timer thread callback (threadpool). Never touch UI directly here.
// We compute the next frame and schedule a UI present via post_task.
static void timer_tick(void* ctx) {
    auto* s = static_cast<AnimState*>(ctx);
    if (!s) return;

    s->tick++;
    fill_pattern(s);

    if (s->plat.post_task) {
        s->plat.post_task(&ui_present, s);
    } else {
        LB_ErrorCode rc = s->plat.win_present_rgba8
            ? s->plat.win_present_rgba8(s->win, s->pixels, s->W, s->H, s->Stride)
            : LB_Error_NotSupported;
        if (rc != LB_Error_Ok) {
            debug_log("lbw_bootstrap: fallback present failed (%d)", rc);
        }
    }
}

// Utility: find the directory of the current executable
static std::string exe_dir() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return ".";
    char* lastSlash = strrchr(buf, '\\');
    if (lastSlash) *lastSlash = '\0';
    return std::string(buf);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Load the platform DLL from next to the EXE
    const std::string dllPath = exe_dir() + "\\ladybird_platform_windows.dll";
    HMODULE dll = LoadLibraryA(dllPath.c_str());
    if (!dll) {
        MessageBoxA(nullptr, ("DLL not found:\n" + dllPath).c_str(), "Error", MB_ICONERROR);
        return 1;
    }

    auto query = reinterpret_cast<LB_QueryPlatformV1Fn>(
        GetProcAddress(dll, "LB_QueryPlatformV1"));
    if (!query) {
        MessageBoxA(nullptr, "LB_QueryPlatformV1 missing", "Error", MB_ICONERROR);
        return 2;
    }

    LB_PlatformV1 plat{};
    LB_ErrorCode query_rc = query(&plat);
    if (query_rc != LB_Error_Ok) {
        MessageBoxA(nullptr, "LB_QueryPlatformV1 failed", "Error", MB_ICONERROR);
        return 3;
    }
    if (plat.abi_version != LB_PLATFORM_ABI_VERSION) {
        MessageBoxA(nullptr, "ABI version mismatch", "Error", MB_ICONERROR);
        return 4;
    }
    if (!plat.init) {
        MessageBoxA(nullptr, "Platform init failed", "Error", MB_ICONERROR);
        return 5;
    }
    LB_ErrorCode init_rc = plat.init();
    if (init_rc != LB_Error_Ok) {
        MessageBoxA(nullptr, "Platform init() failed", "Error", MB_ICONERROR);
        return 6;
    }

    // Create window
    auto* win = plat.win_create(800, 600, "Ladybird Windows — Layer PoC");
    if (!win) {
        MessageBoxA(nullptr, "win_create failed", "Error", MB_ICONERROR);
        plat.shutdown();
        FreeLibrary(dll);
        return 7;
    }

    // Prepare animation state
    static unsigned char pixels[800 * 600 * 4] = {};
    AnimState state{};
    state.plat = plat;
    state.win = win;
    state.W = 800;
    state.H = 600;
    state.Stride = state.W * 4;
    state.pixels = pixels;

    if (plat.win_set_event_callback) {
        plat.win_set_event_callback(win, &handle_platform_event, &state);
    }

    if (plat.fs_stat) {
        LB_FileStat stat{};
        if (plat.fs_stat(dllPath.c_str(), &stat) == LB_Error_Ok) {
            debug_log("DLL size: %llu bytes", static_cast<unsigned long long>(stat.size));
        }
    }
    if (plat.fs_list_directory && plat.buffer_free) {
        LB_DirectoryListing listing{};
        if (plat.fs_list_directory(exe_dir().c_str(), &listing) == LB_Error_Ok) {
            debug_log("Directory entries: %zu", listing.count);
            if (listing.entries.data) {
                plat.buffer_free(listing.entries.data);
            }
        }
    }
    if (plat.clipboard_write_text && plat.clipboard_read_text && plat.buffer_free) {
        plat.clipboard_write_text("Ladybird Windows Platform Clipboard Demo", 0);
        LB_Buffer clip{};
        if (plat.clipboard_read_text(&clip) == LB_Error_Ok && clip.data) {
            debug_log("Clipboard read (%zu bytes): %.*s", clip.size, static_cast<int>(clip.size), clip.data);
            plat.buffer_free(clip.data);
        }
    }
    if (plat.net_request && plat.net_request_cancel) {
        debug_log("Networking API available (WinHTTP backend enabled)");
    }

    // First frame
    fill_pattern(&state);
    if (plat.win_present_rgba8) {
        LB_ErrorCode rc = plat.win_present_rgba8(win, state.pixels, state.W, state.H, state.Stride);
        if (rc != LB_Error_Ok) {
            debug_log("lbw_bootstrap: initial present failed (%d)", rc);
        }
    }

    // If timers are available, animate at ~60 Hz; otherwise just enter loop
    void* timer_handle = nullptr;
    if (plat.timer_start) {
        timer_handle = plat.timer_start(/*ms*/16, /*repeat*/1, &timer_tick, &state);
    }

    // Run message loop (blocks until quit)
    plat.run_event_loop();

    // Best-effort cleanup (timer_stop is optional)
    if (timer_handle && plat.timer_stop) {
        plat.timer_stop(timer_handle);
    }

    plat.shutdown();
    FreeLibrary(dll);
    return 0;
}
