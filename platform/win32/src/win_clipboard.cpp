#include <windows.h>

#include <string>
#include <cstring>
#include <cwchar>
#include <objbase.h>

#include "lb_platform.h"

static std::wstring utf8_to_wide(const char *s, size_t length) {
    if (!s) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s, static_cast<int>(length), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, static_cast<int>(length), out.data(), len);
    return out;
}

static std::string wide_to_utf8(const wchar_t *s, size_t length) {
    if (!s) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(length), out.data(), len, nullptr, nullptr);
    return out;
}

extern "C" LB_ErrorCode clipboard_write_text_impl(const char *utf8, size_t length) {
    if (!utf8 && length > 0) {
        return LB_Error_BadArgument;
    }
    if (!OpenClipboard(nullptr)) {
        return LB_Error_Unknown;
    }
    if (!EmptyClipboard()) {
        CloseClipboard();
        return LB_Error_Unknown;
    }

    std::wstring wide = utf8_to_wide(utf8, length ? length : (utf8 ? strlen(utf8) : 0));
    wide.push_back(L'\0');

    SIZE_T bytes = wide.size() * sizeof(wchar_t);
    HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hmem) {
        CloseClipboard();
        return LB_Error_OutOfMemory;
    }

    void *ptr = GlobalLock(hmem);
    memcpy(ptr, wide.data(), bytes);
    GlobalUnlock(hmem);

    if (!SetClipboardData(CF_UNICODETEXT, hmem)) {
        GlobalFree(hmem);
        CloseClipboard();
        return LB_Error_Unknown;
    }

    CloseClipboard();
    return LB_Error_Ok;
}

extern "C" LB_ErrorCode clipboard_read_text_impl(LB_Buffer *out) {
    if (!out) {
        return LB_Error_BadArgument;
    }
    out->data = nullptr;
    out->size = 0;

    if (!OpenClipboard(nullptr)) {
        return LB_Error_Unknown;
    }

    HANDLE hdata = GetClipboardData(CF_UNICODETEXT);
    if (!hdata) {
        CloseClipboard();
        return LB_Error_NotSupported;
    }

    wchar_t *wtext = static_cast<wchar_t *>(GlobalLock(hdata));
    if (!wtext) {
        CloseClipboard();
        return LB_Error_Unknown;
    }
    size_t len = wcslen(wtext);
    std::string utf8 = wide_to_utf8(wtext, len);
    GlobalUnlock(hdata);
    CloseClipboard();

    if (utf8.empty()) {
        out->data = nullptr;
        out->size = 0;
        return LB_Error_Ok;
    }

    size_t bytes = utf8.size();
    uint8_t *mem = static_cast<uint8_t *>(CoTaskMemAlloc(bytes + 1));
    if (!mem) {
        return LB_Error_OutOfMemory;
    }
    memcpy(mem, utf8.data(), bytes);
    mem[bytes] = '\0';
    out->data = mem;
    out->size = bytes;
    return LB_Error_Ok;
}
