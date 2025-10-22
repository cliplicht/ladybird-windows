#include <windows.h>

#include <string>
#include <vector>
#include <cstring>
#include <objbase.h>

#include "lb_platform.h"

static std::wstring utf8_to_wide(const char *s) {
    if (!s) {
        return {};
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (len <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    if (len > 1) {
        MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), len);
    }
    return out;
}

static std::string wide_to_utf8(const wchar_t *s) {
    if (!s) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len - 1), '\0');
    if (len > 1) {
        WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), len, nullptr, nullptr);
    }
    return out;
}

extern "C" void lbw_buffer_free_impl(void *ptr) {
    if (ptr) {
        CoTaskMemFree(ptr);
    }
}

static LB_ErrorCode read_file_into_buffer(const wchar_t *path, LB_FileResult *out) {
    if (!out) {
        return LB_Error_BadArgument;
    }

    out->buffer.data = nullptr;
    out->buffer.size = 0;

    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            return LB_Error_Unknown;
        }
        return LB_Error_Unknown;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size)) {
        CloseHandle(file);
        return LB_Error_Unknown;
    }
    if (size.QuadPart < 0 || static_cast<unsigned long long>(size.QuadPart) > SIZE_MAX) {
        CloseHandle(file);
        return LB_Error_NotSupported;
    }

    size_t byte_size = static_cast<size_t>(size.QuadPart);
    uint8_t *buffer = nullptr;
    if (byte_size > 0) {
        buffer = static_cast<uint8_t *>(CoTaskMemAlloc(byte_size));
        if (!buffer) {
            CloseHandle(file);
            return LB_Error_OutOfMemory;
        }

        DWORD total_read = 0;
        while (total_read < byte_size) {
            DWORD chunk = 0;
            if (!ReadFile(file, buffer + total_read, static_cast<DWORD>(byte_size - total_read), &chunk, nullptr)) {
                CloseHandle(file);
                lbw_buffer_free_impl(buffer);
                return LB_Error_Unknown;
            }
            if (chunk == 0) {
                break;
            }
            total_read += chunk;
        }
        byte_size = total_read;
    }

    CloseHandle(file);
    out->buffer.data = buffer;
    out->buffer.size = byte_size;
    return LB_Error_Ok;
}

extern "C" LB_ErrorCode fs_read_entire_file_impl(const char *path_utf8, LB_FileResult *out) {
    if (!path_utf8 || !out) {
        return LB_Error_BadArgument;
    }
    std::wstring path = utf8_to_wide(path_utf8);
    if (path.empty()) {
        return LB_Error_BadArgument;
    }
    return read_file_into_buffer(path.c_str(), out);
}

extern "C" LB_ErrorCode fs_write_entire_file_impl(const char *path_utf8, const void *data, size_t size) {
    if (!path_utf8 || (!data && size > 0)) {
        return LB_Error_BadArgument;
    }
    std::wstring path = utf8_to_wide(path_utf8);
    if (path.empty()) {
        return LB_Error_BadArgument;
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return LB_Error_Unknown;
    }

    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    size_t remaining = size;
    while (remaining > 0) {
        DWORD written = 0;
        DWORD to_write = remaining > MAXDWORD ? MAXDWORD : static_cast<DWORD>(remaining);
        if (!WriteFile(file, bytes, to_write, &written, nullptr)) {
            CloseHandle(file);
            return LB_Error_Unknown;
        }
        bytes += written;
        remaining -= written;
    }

    CloseHandle(file);
    return LB_Error_Ok;
}

extern "C" LB_ErrorCode fs_remove_file_impl(const char *path_utf8) {
    if (!path_utf8) {
        return LB_Error_BadArgument;
    }
    std::wstring path = utf8_to_wide(path_utf8);
    if (path.empty()) {
        return LB_Error_BadArgument;
    }
    if (DeleteFileW(path.c_str())) {
        return LB_Error_Ok;
    }
    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND) {
        return LB_Error_Ok;
    }
    return LB_Error_Unknown;
}

static void populate_stat_from_info(const WIN32_FILE_ATTRIBUTE_DATA &info, LB_FileStat *out) {
    if (!out) return;
    ULARGE_INTEGER size{};
    size.HighPart = info.nFileSizeHigh;
    size.LowPart = info.nFileSizeLow;
    ULARGE_INTEGER ft{};
    ft.HighPart = info.ftLastWriteTime.dwHighDateTime;
    ft.LowPart = info.ftLastWriteTime.dwLowDateTime;
    out->size = size.QuadPart;
    out->modified_timestamp = ft.QuadPart;
    out->attributes = info.dwFileAttributes;
    out->is_directory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
    out->reserved[0] = out->reserved[1] = out->reserved[2] = 0;
}

extern "C" LB_ErrorCode fs_stat_impl(const char *path_utf8, LB_FileStat *out) {
    if (!path_utf8 || !out) {
        return LB_Error_BadArgument;
    }
    std::wstring path = utf8_to_wide(path_utf8);
    if (path.empty()) {
        return LB_Error_BadArgument;
    }
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
        return LB_Error_Unknown;
    }
    populate_stat_from_info(info, out);
    return LB_Error_Ok;
}

extern "C" LB_ErrorCode fs_list_directory_impl(const char *path_utf8, LB_DirectoryListing *out) {
    if (!path_utf8 || !out) {
        return LB_Error_BadArgument;
    }
    std::wstring path = utf8_to_wide(path_utf8);
    if (path.empty()) {
        return LB_Error_BadArgument;
    }

    std::wstring pattern = path;
    if (!pattern.empty()) {
        if (pattern.back() != L'\\' && pattern.back() != L'/') {
            pattern.push_back(L'\\');
        }
    }
    pattern.append(L"*");

    WIN32_FIND_DATAW data;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &data);
    if (hFind == INVALID_HANDLE_VALUE) {
        out->entries.data = nullptr;
        out->entries.size = 0;
        out->count = 0;
        return LB_Error_Unknown;
    }

    std::vector<std::string> names;
    size_t total_bytes = 1; // final double null terminator
    do {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
            continue;
        }
        std::string u8 = wide_to_utf8(data.cFileName);
        names.push_back(u8);
        total_bytes += u8.size() + 1;
    } while (FindNextFileW(hFind, &data));

    FindClose(hFind);

    if (names.empty()) {
        total_bytes = 2; // double null terminator even when empty
    }

    char *mem = nullptr;
    if (total_bytes > 0) {
        mem = static_cast<char *>(CoTaskMemAlloc(total_bytes));
        if (!mem) {
            out->entries.data = nullptr;
            out->entries.size = 0;
            out->count = 0;
            return LB_Error_OutOfMemory;
        }
        size_t offset = 0;
        for (const auto &name : names) {
            memcpy(mem + offset, name.c_str(), name.size());
            offset += name.size();
            mem[offset++] = '\0';
        }
        mem[offset++] = '\0';
        if (offset < total_bytes) {
            mem[offset++] = '\0';
        }
        out->entries.data = reinterpret_cast<uint8_t *>(mem);
        out->entries.size = offset;
    } else {
        out->entries.data = nullptr;
        out->entries.size = 0;
    }
    out->count = names.size();
    return LB_Error_Ok;
}
