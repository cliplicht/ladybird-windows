#include <windows.h>
#include <windowsx.h>
#include <imm.h>
#include <shellapi.h>
#include <objbase.h>
#include <string>
#include <vector>
#include <memory>
#include <cstring>

#include "lb_platform.h"
#include "win_window_internal.h"

extern "C" void lbw_set_task_hwnd(HWND hwnd);
extern "C" void lbw_clear_task_hwnd(HWND hwnd);
extern "C" void lbw_pump_posted_tasks();

// --- simple UTF-8 -> UTF-16 helper ---
static std::wstring utf8_to_wide(const char *s) {
    if (!s) return {};
    int lenW = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(lenW > 0 ? lenW : 0, L'\0');
    if (lenW > 0) {
        int written = MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), lenW);
        if (written > 0) {
            w.resize(written - 1); // drop the terminating null
        } else {
            w.clear();
        }
    }
    return w;
}

static std::string wide_to_utf8(const wchar_t *s, int length = -1) {
    if (!s) return {};
    int lenU8 = WideCharToMultiByte(CP_UTF8, 0, s, length, nullptr, 0, nullptr, nullptr);
    if (lenU8 <= 0) return {};
    std::string out(static_cast<size_t>(lenU8), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, length, out.data(), lenU8, nullptr, nullptr);
    if (length == -1 && !out.empty() && out.back() == '\0') {
        out.pop_back();
    }
    return out;
}

static UINT get_dpi_for_window_safe(HWND h) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using GetDpiForWindowFn = UINT(WINAPI *)(HWND);
        if (auto fn = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"))) {
            return fn(h);
        }
    }
    HDC hdc = GetDC(h);
    UINT dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSX) : 96;
    if (hdc) ReleaseDC(h, hdc);
    return dpi ? dpi : 96;
}

static uint32_t query_modifiers() {
    uint32_t mods = LB_Mod_None;
    if (GetKeyState(VK_SHIFT) & 0x8000) mods |= LB_Mod_Shift;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= LB_Mod_Ctrl;
    if (GetKeyState(VK_MENU) & 0x8000) mods |= LB_Mod_Alt;
    if (GetKeyState(VK_LWIN) & 0x8000 || GetKeyState(VK_RWIN) & 0x8000) mods |= LB_Mod_Super;
    if (GetKeyState(VK_CAPITAL) & 0x0001) mods |= LB_Mod_Caps;
    if (GetKeyState(VK_NUMLOCK) & 0x0001) mods |= LB_Mod_Num;
    return mods;
}

static void dispatch_event(lb_window *win, LB_Event &event) {
    if (!win || !win->event_cb) {
        return;
    }
    event.window = win;
    win->event_cb(&event, win->event_ctx);
}

static void update_window_metrics(lb_window *win) {
    if (!win || !win->hwnd) return;
    win->dpi = get_dpi_for_window_safe(win->hwnd);
    win->scale = static_cast<float>(win->dpi) / 96.0f;
    GetClientRect(win->hwnd, &win->client_rect);
    win->width = win->client_rect.right - win->client_rect.left;
    win->height = win->client_rect.bottom - win->client_rect.top;
    if (win->width < 0) win->width = 0;
    if (win->height < 0) win->height = 0;
    if (win->use_d3d && win->d3d_swap_chain) {
        lbw_d3d_resize(win, win->width, win->height);
    }
}

// keep a single registered class
static ATOM ensure_class(HINSTANCE hInst) {
    static ATOM atom = 0;
    if (atom) return atom;
    WNDCLASSW wc{};
    wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        if (m == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW *>(l);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        auto *win = reinterpret_cast<lb_window *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        switch (m) {
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                if (win) {
                    LB_Event ev{};
                    ev.type = LB_Event_KeyDown;
                    ev.data.key.virtual_key = static_cast<uint32_t>(w);
                    ev.data.key.scan_code = static_cast<uint32_t>((l >> 16) & 0xFF);
                    ev.data.key.modifiers = query_modifiers();
                    ev.data.key.is_repeat = (l & (1 << 30)) ? 1 : 0;
                    dispatch_event(win, ev);
                }
                return 0;
            case WM_KEYUP:
            case WM_SYSKEYUP:
                if (win) {
                    LB_Event ev{};
                    ev.type = LB_Event_KeyUp;
                    ev.data.key.virtual_key = static_cast<uint32_t>(w);
                    ev.data.key.scan_code = static_cast<uint32_t>((l >> 16) & 0xFF);
                    ev.data.key.modifiers = query_modifiers();
                    ev.data.key.is_repeat = 0;
                    dispatch_event(win, ev);
                }
                return 0;
            case WM_CHAR:
            case WM_SYSCHAR:
                if (win) {
                    uint32_t code_unit = static_cast<uint32_t>(w);
                    uint32_t code_point = code_unit;
                    if (code_unit >= 0xD800 && code_unit <= 0xDBFF) {
                        win->pending_surrogate = static_cast<uint16_t>(code_unit);
                        return 0;
                    }
                    if (code_unit >= 0xDC00 && code_unit <= 0xDFFF && win->pending_surrogate) {
                        uint32_t high = win->pending_surrogate;
                        win->pending_surrogate = 0;
                        code_point = 0x10000 + (((high - 0xD800) << 10) | (code_unit - 0xDC00));
                    } else {
                        win->pending_surrogate = 0;
                    }
                    LB_Event ev{};
                    ev.type = LB_Event_Text;
                    ev.data.text.code_point = code_point;
                    ev.data.text.modifiers = query_modifiers();
                    dispatch_event(win, ev);
                }
                return 0;
            case WM_SIZE:
                if (win) {
                    update_window_metrics(win);
                    LB_Event ev{};
                    ev.type = LB_Event_WindowResize;
                    ev.data.resize.width = win->width;
                    ev.data.resize.height = win->height;
                    ev.data.resize.dpi = win->dpi;
                    ev.data.resize.scale = win->scale;
                    dispatch_event(win, ev);
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            case WM_CLOSE:
                if (win) {
                    win->wants_close = true;
                    LB_Event ev{};
                    ev.type = LB_Event_WindowClose;
                    dispatch_event(win, ev);
                }
                DestroyWindow(h);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            case WM_APP + 1:
                lbw_pump_posted_tasks();
                return 0;
            case WM_SETFOCUS:
            case WM_KILLFOCUS:
                if (win) {
                    LB_Event ev{};
                    ev.type = LB_Event_WindowFocus;
                    ev.data.focus.focused = (m == WM_SETFOCUS) ? 1 : 0;
                    dispatch_event(win, ev);
                }
                return 0;
            case WM_MOUSEMOVE:
                if (win) {
                    int x = GET_X_LPARAM(l);
                    int y = GET_Y_LPARAM(l);
                    int dx = 0;
                    int dy = 0;
                    if (win->has_last_pos) {
                        dx = x - win->last_x;
                        dy = y - win->last_y;
                    }
                    win->last_x = x;
                    win->last_y = y;
                    win->has_last_pos = true;

                    if (!win->tracking_mouse) {
                        TRACKMOUSEEVENT tme{};
                        tme.cbSize = sizeof(tme);
                        tme.dwFlags = TME_LEAVE;
                        tme.hwndTrack = h;
                        TrackMouseEvent(&tme);
                        win->tracking_mouse = true;
                    }

                    LB_Event ev{};
                    ev.type = LB_Event_MouseMove;
                    ev.data.mouse.x = x;
                    ev.data.mouse.y = y;
                    ev.data.mouse.delta_x = dx;
                    ev.data.mouse.delta_y = dy;
                    ev.data.mouse.wheel_delta_x = 0;
                    ev.data.mouse.wheel_delta_y = 0;
                    ev.data.mouse.modifiers = query_modifiers();
                    ev.data.mouse.button = LB_MouseButton_None;
                    dispatch_event(win, ev);
                }
                return 0;
            case WM_MOUSELEAVE:
                if (win) {
                    win->tracking_mouse = false;
                    win->has_last_pos = false;
                    LB_Event ev{};
                    ev.type = LB_Event_MouseLeave;
                    ev.data.mouse.x = win->last_x;
                    ev.data.mouse.y = win->last_y;
                    ev.data.mouse.delta_x = 0;
                    ev.data.mouse.delta_y = 0;
                    ev.data.mouse.wheel_delta_x = 0;
                    ev.data.mouse.wheel_delta_y = 0;
                    ev.data.mouse.modifiers = query_modifiers();
                    ev.data.mouse.button = LB_MouseButton_None;
                    dispatch_event(win, ev);
                }
                return 0;
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_XBUTTONDOWN:
                if (win) {
                    SetCapture(h);
                    LB_MouseButton button = LB_MouseButton_Left;
                    if (m == WM_RBUTTONDOWN) button = LB_MouseButton_Right;
                    else if (m == WM_MBUTTONDOWN) button = LB_MouseButton_Middle;
                    else if (m == WM_XBUTTONDOWN) {
                        button = (GET_XBUTTON_WPARAM(w) == XBUTTON1) ? LB_MouseButton_X1 : LB_MouseButton_X2;
                    }
                    int x = GET_X_LPARAM(l);
                    int y = GET_Y_LPARAM(l);
                    win->last_x = x;
                    win->last_y = y;
                    win->has_last_pos = true;
                    LB_Event ev{};
                    ev.type = LB_Event_MouseDown;
                    ev.data.mouse.x = x;
                    ev.data.mouse.y = y;
                    ev.data.mouse.delta_x = 0;
                    ev.data.mouse.delta_y = 0;
                    ev.data.mouse.wheel_delta_x = 0;
                    ev.data.mouse.wheel_delta_y = 0;
                    ev.data.mouse.modifiers = query_modifiers();
                    ev.data.mouse.button = button;
                    dispatch_event(win, ev);
                }
                return 0;
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP:
            case WM_XBUTTONUP:
                if (win) {
                    if (!(w & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2))) {
                        ReleaseCapture();
                    }
                    LB_MouseButton button = LB_MouseButton_Left;
                    if (m == WM_RBUTTONUP) button = LB_MouseButton_Right;
                    else if (m == WM_MBUTTONUP) button = LB_MouseButton_Middle;
                    else if (m == WM_XBUTTONUP) {
                        button = (GET_XBUTTON_WPARAM(w) == XBUTTON1) ? LB_MouseButton_X1 : LB_MouseButton_X2;
                    }
                    int x = GET_X_LPARAM(l);
                    int y = GET_Y_LPARAM(l);
                    LB_Event ev{};
                    ev.type = LB_Event_MouseUp;
                    ev.data.mouse.x = x;
                    ev.data.mouse.y = y;
                    ev.data.mouse.delta_x = 0;
                    ev.data.mouse.delta_y = 0;
                    ev.data.mouse.wheel_delta_x = 0;
                    ev.data.mouse.wheel_delta_y = 0;
                    ev.data.mouse.modifiers = query_modifiers();
                    ev.data.mouse.button = button;
                    dispatch_event(win, ev);
                }
                return 0;
            case WM_MOUSEWHEEL:
                if (win) {
                    POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
                    ScreenToClient(h, &pt);
                    int delta = GET_WHEEL_DELTA_WPARAM(w);
                    LB_Event ev{};
                    ev.type = LB_Event_MouseWheel;
                    ev.data.mouse.x = pt.x;
                    ev.data.mouse.y = pt.y;
                    ev.data.mouse.delta_x = 0;
                    ev.data.mouse.delta_y = 0;
                    ev.data.mouse.wheel_delta_x = 0;
                    ev.data.mouse.wheel_delta_y = delta;
                    ev.data.mouse.modifiers = query_modifiers();
                    ev.data.mouse.button = LB_MouseButton_None;
                    dispatch_event(win, ev);
                }
                return 0;
            case WM_MOUSEHWHEEL:
                if (win) {
                    POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
                    ScreenToClient(h, &pt);
                    int delta = GET_WHEEL_DELTA_WPARAM(w);
                    LB_Event ev{};
                    ev.type = LB_Event_MouseWheel;
                    ev.data.mouse.x = pt.x;
                    ev.data.mouse.y = pt.y;
                    ev.data.mouse.delta_x = 0;
                    ev.data.mouse.delta_y = 0;
                    ev.data.mouse.wheel_delta_x = delta;
                    ev.data.mouse.wheel_delta_y = 0;
                    ev.data.mouse.modifiers = query_modifiers();
                    ev.data.mouse.button = LB_MouseButton_None;
                    dispatch_event(win, ev);
                }
                return 0;
            case WM_IME_STARTCOMPOSITION:
                if (win) {
                    LB_Event ev{};
                    ev.type = LB_Event_ImeStart;
                    ev.data.ime.text_utf8 = nullptr;
                    ev.data.ime.length = 0;
                    ev.data.ime.cursor_begin = 0;
                    ev.data.ime.cursor_end = 0;
                    dispatch_event(win, ev);
                }
                return 0;
            case WM_IME_ENDCOMPOSITION:
                if (win) {
                    LB_Event ev{};
                    ev.type = LB_Event_ImeEnd;
                    ev.data.ime.text_utf8 = nullptr;
                    ev.data.ime.length = 0;
                    ev.data.ime.cursor_begin = 0;
                    ev.data.ime.cursor_end = 0;
                    dispatch_event(win, ev);
                }
                return 0;
            case WM_IME_COMPOSITION:
                if (win) {
                    HIMC himc = ImmGetContext(h);
                    std::string composition;
                    uint32_t cursor_pos = 0;
                    if (himc) {
                        LONG bytes = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
                        if (bytes > 0) {
                            std::wstring wcomp(static_cast<size_t>(bytes / sizeof(wchar_t)), L'\0');
                            ImmGetCompositionStringW(himc, GCS_COMPSTR, wcomp.data(), bytes);
                            composition = wide_to_utf8(wcomp.c_str(), static_cast<int>(wcomp.size()));
                        }
                        LONG cpos = ImmGetCompositionStringW(himc, GCS_CURSORPOS, nullptr, 0);
                        if (cpos > 0) {
                            cursor_pos = static_cast<uint32_t>(cpos);
                        }
                        ImmReleaseContext(h, himc);
                    }

                    char *mem = nullptr;
                    if (!composition.empty()) {
                        mem = static_cast<char *>(CoTaskMemAlloc(composition.size()));
                        if (mem) {
                            memcpy(mem, composition.data(), composition.size());
                        }
                    }
                    LB_Event ev{};
                    ev.type = LB_Event_ImeComposition;
                    ev.data.ime.text_utf8 = mem;
                    ev.data.ime.length = composition.size();
                    ev.data.ime.cursor_begin = cursor_pos;
                    ev.data.ime.cursor_end = cursor_pos;
                    dispatch_event(win, ev);
                    if (mem) {
                        CoTaskMemFree(mem);
                    }
                }
                return 0;
            case WM_DROPFILES:
                if (win) {
                    HDROP drop = reinterpret_cast<HDROP>(w);
                    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                    size_t total_bytes = 1; // final double-null
                    std::vector<std::string> utf8_paths;
                    utf8_paths.reserve(count);
                    for (UINT i = 0; i < count; ++i) {
                        UINT len = DragQueryFileW(drop, i, nullptr, 0);
                        std::wstring wpath(len + 1, L'\0');
                        DragQueryFileW(drop, i, wpath.data(), len + 1);
                        std::string u8 = wide_to_utf8(wpath.c_str());
                        utf8_paths.push_back(u8);
                        total_bytes += u8.size() + 1;
                    }
                    if (utf8_paths.empty()) {
                        total_bytes = 2;
                    }
                    char *mem = nullptr;
                    if (total_bytes > 0) {
                        mem = static_cast<char *>(CoTaskMemAlloc(total_bytes));
                    }
                    size_t offset = 0;
                    if (mem) {
                        for (const auto &path : utf8_paths) {
                            memcpy(mem + offset, path.c_str(), path.size());
                            offset += path.size();
                            mem[offset++] = '\0';
                        }
                        mem[offset++] = '\0';
                        if (offset < total_bytes) {
                            mem[offset++] = '\0';
                        }
                    }
                    LB_Event ev{};
                    ev.type = LB_Event_DropFiles;
                    ev.data.drop.paths_utf8 = mem;
                    ev.data.drop.size = total_bytes;
                    ev.data.drop.count = utf8_paths.size();
                    dispatch_event(win, ev);
                    if (mem) {
                        CoTaskMemFree(mem);
                    }
                    DragFinish(drop);
                }
                return 0;
            case WM_PAINT:
                if (win) {
                    PAINTSTRUCT ps;
                    HDC hdc = BeginPaint(h, &ps);
                    if (win->use_d3d && (!win->pixels || win->pixel_width <= 0 || win->pixel_height <= 0)) {
                        // D3D path handled via Present; nothing to draw.
                    } else if (win->pixels && win->pixel_width > 0 && win->pixel_height > 0) {
                        SetStretchBltMode(hdc, HALFTONE);
                        SetBrushOrgEx(hdc, 0, 0, nullptr);
                        StretchDIBits(
                            hdc,
                            0, 0, win->width, win->height, // dst
                            0, 0, win->pixel_width, win->pixel_height, // src
                            win->pixels,
                            &win->bmi,
                            DIB_RGB_COLORS,
                            SRCCOPY
                        );
                        win->needs_present = false;
                    } else {
                        FillRect(hdc, &ps.rcPaint, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
                    }
                    EndPaint(h, &ps);
                    return 0;
                }
                break;
            case WM_DPICHANGED:
                if (win) {
                    const RECT *suggested = reinterpret_cast<RECT *>(l);
                    SetWindowPos(h, nullptr,
                                 suggested->left, suggested->top,
                                 suggested->right - suggested->left,
                                 suggested->bottom - suggested->top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                    update_window_metrics(win);
                    LB_Event ev{};
                    ev.type = LB_Event_WindowResize;
                    ev.data.resize.width = win->width;
                    ev.data.resize.height = win->height;
                    ev.data.resize.dpi = win->dpi;
                    ev.data.resize.scale = win->scale;
                    dispatch_event(win, ev);
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
        }
        return DefWindowProcW(h, m, w, l);
    };
    wc.hInstance = hInst;
    wc.lpszClassName = L"LadybirdWinPlatform";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    atom = RegisterClassW(&wc);
    return atom;
}

extern "C" lb_window *win_create_impl(int w, int h, const char *title_utf8) {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    if (!ensure_class(hInst)) return nullptr;

    auto *win = new lb_window{};

    // init BITMAPINFO for RGBA8 top-down
    win->bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    win->bmi.bmiHeader.biPlanes = 1;
    win->bmi.bmiHeader.biBitCount = 32;
    win->bmi.bmiHeader.biCompression = BI_RGB;
    win->bmi.bmiHeader.biWidth = 0;
    win->bmi.bmiHeader.biHeight = 0;

    std::wstring wtitle = utf8_to_wide(title_utf8);
    HWND hwnd = CreateWindowExW(
        0, L"LadybirdWinPlatform",
        wtitle.empty() ? L"Ladybird Windows — PoC" : wtitle.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, w, h,
        nullptr, nullptr, hInst, win
    );

    if (!hwnd) {
        lbw_log("lb_platform: CreateWindowExW failed (err=%lu)", static_cast<unsigned long>(GetLastError()));
        delete win;
        return nullptr;
    }
    win->hwnd = hwnd;
    lbw_set_task_hwnd(hwnd);
    DragAcceptFiles(hwnd, TRUE);

    update_window_metrics(win);

    if (lbw_d3d_init(win, win->width, win->height)) {
        win->use_d3d = true;
        lbw_log("lb_platform: D3D11 presentation enabled");
    } else {
        lbw_log("lb_platform: Using GDI presentation");
    }

    // Adjust the window size to account for DPI scaling.
    const float desired_scale = win->scale;
    SetWindowPos(hwnd, nullptr, 0, 0,
                 static_cast<int>(w * desired_scale),
                 static_cast<int>(h * desired_scale),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    update_window_metrics(win);
    return win;
}

extern "C" void win_destroy_impl(lb_window *w) {
    if (!w) return;
    if (w->hwnd) {
        DragAcceptFiles(w->hwnd, FALSE);
        lbw_d3d_destroy(w);
        lbw_clear_task_hwnd(w->hwnd);
        DestroyWindow(w->hwnd);
    }
    delete w;
}

extern "C" LB_ErrorCode win_present_rgba8_impl(lb_window *w, const void *pixels, int pw, int ph, int stride) {
    if (!w || !w->hwnd || !pixels) {
        return LB_Error_BadArgument;
    }
    if (pw <= 0 || ph <= 0 || stride <= 0) {
        return LB_Error_BadArgument;
    }
    if (stride < pw * 4) {
        return LB_Error_BadArgument;
    }

    if (w->use_d3d) {
        LB_ErrorCode rc = lbw_d3d_present(w, pixels, pw, ph, stride);
        if (rc == LB_Error_Ok) {
            w->pixels = nullptr;
            w->pixel_width = 0;
            w->pixel_height = 0;
            w->pixel_stride = 0;
            w->bmi.bmiHeader.biWidth = 0;
            w->bmi.bmiHeader.biHeight = 0;
            w->needs_present = false;
            return rc;
        }
        if (rc == LB_Error_Unknown) {
            lbw_d3d_destroy(w);
        }
        // Not supported (e.g., size mismatch) falls through to GDI path.
    }

    w->pixels = pixels;
    w->pixel_width = pw;
    w->pixel_height = ph;
    w->pixel_stride = stride;
    w->bmi.bmiHeader.biWidth = pw;
    w->bmi.bmiHeader.biHeight = -ph; // top-down
    w->bmi.bmiHeader.biSizeImage = static_cast<DWORD>(stride) * static_cast<DWORD>(ph);
    bool requested_paint = w->needs_present;
    w->needs_present = true;

    if (!requested_paint && !InvalidateRect(w->hwnd, nullptr, FALSE)) {
        lbw_log("lb_platform: InvalidateRect failed (err=%lu)", static_cast<unsigned long>(GetLastError()));
        return LB_Error_Unknown;
    }

    return LB_Error_Ok;
}

extern "C" void win_set_event_callback_impl(lb_window *w, LB_EventCallback cb, void *ctx) {
    if (!w) {
        return;
    }
    w->event_cb = cb;
    w->event_ctx = ctx;
}
