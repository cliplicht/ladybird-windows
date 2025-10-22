#include <windows.h>
#include <string>

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

static UINT get_dpi_for_windows(HWND h) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
        auto fn = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
        if (fn) return fn(h);
    }
    return 96;
}

// --- lb_window type expected by the header ---
struct lb_window {
    HWND hwnd{};
    BITMAPINFO bmi{};
    int width{};
    int height{};
    bool wants_close{};
};

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
            case WM_SIZE:
                if (win) {
                    win->width = LOWORD(l);
                    win->height = HIWORD(l);
                    win->bmi.bmiHeader.biWidth = win->width;
                    win->bmi.bmiHeader.biHeight = -win->height; // top-down
                }
                return 0;
            case WM_CLOSE:
                if (win) win->wants_close = true;
                DestroyWindow(h);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            case WM_DPICHANGED:
                const RECT *suggested = reinterpret_cast<RECT *>(l);
                SetWindowPos(h, nullptr,
                             suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
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
    win->width = w;
    win->height = h;

    // init BITMAPINFO for RGBA8 top-down
    win->bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    win->bmi.bmiHeader.biWidth = w;
    win->bmi.bmiHeader.biHeight = -h; // top-down
    win->bmi.bmiHeader.biPlanes = 1;
    win->bmi.bmiHeader.biBitCount = 32;
    win->bmi.bmiHeader.biCompression = BI_RGB;

    std::wstring wtitle = utf8_to_wide(title_utf8);
    HWND hwnd = CreateWindowExW(
        0, L"LadybirdWinPlatform",
        wtitle.empty() ? L"Ladybird Windows — PoC" : wtitle.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, w, h,
        nullptr, nullptr, hInst, win
    );

    const UINT dpi = get_dpi_for_windows(hwnd);
    float scale = dpi / 96.0f;
    SetWindowPos(hwnd, nullptr, 0, 0,
                 int(w * scale), int(h * scale), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (!hwnd) {
        delete win;
        return nullptr;
    }
    win->hwnd = hwnd;
    return win;
}

extern "C" void win_destroy_impl(lb_window *w) {
    if (!w) return;
    if (w->hwnd) DestroyWindow(w->hwnd);
    delete w;
}

extern "C" void win_present_rgba8_impl(lb_window *w, const void *pixels, int pw, int ph, int stride) {
    if (!w || !w->hwnd || !pixels) return;
    HDC hdc = GetDC(w->hwnd);
    // Target dimensions: window size (w->width/height), source dimensions: buffer (pw/ph)
    StretchDIBits(
        hdc,
        0, 0, w->width, w->height, // dst
        0, 0, pw, ph, // src
        pixels,
        &w->bmi,
        DIB_RGB_COLORS,
        SRCCOPY
    );
    ReleaseDC(w->hwnd, hdc);
}
