#include <windows.h>
#include <winhttp.h>
#include <process.h>

#include <atomic>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>
#include <objbase.h>

#include "lb_platform.h"

extern "C" void post_task_impl(void (*fn)(void *), void *ctx);

static std::wstring utf8_to_wide(const char *s) {
    if (!s) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    if (len > 1) {
        MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), len);
    }
    return out;
}

static std::string wide_to_utf8(const wchar_t *s, size_t length = static_cast<size_t>(-1)) {
    if (!s) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(length), out.data(), len, nullptr, nullptr);
    if (length == static_cast<size_t>(-1) && !out.empty() && out.back() == '\0')
        out.pop_back();
    return out;
}

struct lb_net_request {
    std::atomic<bool> cancelled{false};
    std::atomic<bool> callback_scheduled{false};
    std::atomic<bool> finalized{false};
    LB_NetResponseCallback callback{};
    void *callback_ctx{};
    HANDLE completion_event{nullptr};
    HANDLE thread_handle{nullptr};
    std::wstring host;
    std::wstring path_and_query;
    std::wstring method_w;
    INTERNET_PORT port{};
    bool secure{};
    std::vector<std::pair<std::wstring, std::wstring>> headers;
    std::vector<uint8_t> body;
};

struct NetResponsePayload {
    LB_NetResponse response{};
    lb_net_request *request{};
};

static void free_response_buffers(LB_NetResponse &resp) {
    if (resp.body.data) {
        CoTaskMemFree(resp.body.data);
        resp.body.data = nullptr;
        resp.body.size = 0;
    }
    if (resp.headers) {
        CoTaskMemFree(const_cast<LB_NetHeader *>(resp.headers));
        resp.headers = nullptr;
        resp.header_count = 0;
    }
}

static void finalize_request(lb_net_request *req) {
    if (!req) return;
    bool expected = false;
    if (!req->finalized.compare_exchange_strong(expected, true)) {
        return;
    }
    if (req->completion_event) {
        WaitForSingleObject(req->completion_event, INFINITE);
    }
    if (req->thread_handle) {
        CloseHandle(req->thread_handle);
    }
    if (req->completion_event) {
        CloseHandle(req->completion_event);
    }
    delete req;
}

static void deliver_response(void *ctx) {
    std::unique_ptr<NetResponsePayload> payload(static_cast<NetResponsePayload *>(ctx));
    if (!payload || !payload->request) {
        return;
    }

    lb_net_request *req = payload->request;
    if (!req->cancelled.load() && req->callback) {
        req->callback(&payload->response, req->callback_ctx);
    }

    finalize_request(req);
}

static unsigned __stdcall network_thread_proc(void *param) {
    lb_net_request *req = static_cast<lb_net_request *>(param);

    LB_NetResponse response{};
    response.error = LB_Error_Unknown;

    HINTERNET session = nullptr;
    HINTERNET connect = nullptr;
    HINTERNET request = nullptr;

    do {
        session = WinHttpOpen(L"LadybirdPlatform/1.0",
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS,
                              0);
        if (!session) {
            break;
        }

        connect = WinHttpConnect(session, req->host.c_str(), req->port, 0);
        if (!connect) {
            break;
        }

        const wchar_t *verb = req->method_w.empty() ? L"GET" : req->method_w.c_str();
        DWORD open_flags = req->secure ? WINHTTP_FLAG_SECURE : 0;
        request = WinHttpOpenRequest(connect,
                                     verb,
                                     req->path_and_query.c_str(),
                                     nullptr,
                                     WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     open_flags);
        if (!request) {
            break;
        }

        for (const auto &hdr : req->headers) {
            std::wstring header_line = hdr.first + L": " + hdr.second + L"\r\n";
            WinHttpAddRequestHeaders(request, header_line.c_str(),
                                     static_cast<DWORD>(header_line.size()),
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }

        DWORD body_size = req->body.empty() ? 0 : static_cast<DWORD>(req->body.size());
        LPVOID body_ptr = req->body.empty() ? WINHTTP_NO_REQUEST_DATA : req->body.data();

        if (!WinHttpSendRequest(request,
                                WINHTTP_NO_ADDITIONAL_HEADERS,
                                0,
                                body_ptr,
                                body_size,
                                body_size,
                                0)) {
            break;
        }

        if (req->cancelled.load()) {
            break;
        }

        if (!WinHttpReceiveResponse(request, nullptr)) {
            break;
        }

        if (req->cancelled.load()) {
            break;
        }

        DWORD status = 0;
        DWORD status_size = sizeof(status);
        if (WinHttpQueryHeaders(request,
                                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX,
                                &status,
                                &status_size,
                                WINHTTP_NO_HEADER_INDEX)) {
            response.http_status = status;
        }

        std::vector<uint8_t> body_storage;
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available)) {
            if (available == 0) {
                break;
            }
            size_t old_size = body_storage.size();
            body_storage.resize(old_size + available);
            DWORD read = 0;
            if (!WinHttpReadData(request, body_storage.data() + old_size, available, &read)) {
                body_storage.resize(old_size);
                break;
            }
            body_storage.resize(old_size + read);
            if (req->cancelled.load()) {
                break;
            }
        }

        if (req->cancelled.load()) {
            body_storage.clear();
        }

        if (!body_storage.empty()) {
            uint8_t *mem = static_cast<uint8_t *>(CoTaskMemAlloc(body_storage.size()));
            if (!mem) {
                response.error = LB_Error_OutOfMemory;
                break;
            }
            memcpy(mem, body_storage.data(), body_storage.size());
            response.body.data = mem;
            response.body.size = body_storage.size();
            response.error = LB_Error_Ok;
        } else {
            response.body.data = nullptr;
            response.body.size = 0;
            response.error = req->cancelled.load() ? LB_Error_Unknown : LB_Error_Ok;
        }

        // Parse raw headers into UTF-8 pairs
        if (response.error == LB_Error_Ok) {
            DWORD raw_size = 0;
            if (!WinHttpQueryHeaders(request,
                                     WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                     WINHTTP_HEADER_NAME_BY_INDEX,
                                     nullptr,
                                     &raw_size,
                                     WINHTTP_NO_HEADER_INDEX) && GetLastError() == ERROR_INSUFFICIENT_BUFFER && raw_size > 0) {
                std::vector<wchar_t> raw(raw_size / sizeof(wchar_t));
                if (WinHttpQueryHeaders(request,
                                         WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                         WINHTTP_HEADER_NAME_BY_INDEX,
                                         raw.data(),
                                         &raw_size,
                                         WINHTTP_NO_HEADER_INDEX)) {
                    size_t char_count = raw_size / sizeof(wchar_t);
                    if (char_count > 0) {
                        raw.resize(char_count);
                        const wchar_t *cursor = raw.data();
                        if (*cursor) {
                            cursor += wcslen(cursor) + 1; // skip status line
                        }
                        std::vector<std::pair<std::string, std::string>> parsed_headers;
                        while (*cursor) {
                            const wchar_t *line = cursor;
                            cursor += wcslen(cursor) + 1;
                            const wchar_t *colon = wcschr(line, L':');
                            if (!colon) {
                                continue;
                            }
                            const wchar_t *value = colon + 1;
                            while (*value == L' ') ++value;
                            std::wstring name_w(line, colon - line);
                            std::wstring value_w(value);
                            parsed_headers.emplace_back(wide_to_utf8(name_w.c_str(), name_w.size()),
                                                         wide_to_utf8(value_w.c_str(), value_w.size()));
                        }

                        if (!parsed_headers.empty()) {
                            size_t count = parsed_headers.size();
                            size_t total_string_bytes = 0;
                            for (const auto &entry : parsed_headers) {
                                total_string_bytes += entry.first.size() + 1;
                                total_string_bytes += entry.second.size() + 1;
                            }
                            size_t alloc_size = sizeof(LB_NetHeader) * count + total_string_bytes;
                            uint8_t *header_mem = static_cast<uint8_t *>(CoTaskMemAlloc(alloc_size));
                            if (header_mem) {
                                auto *header_array = reinterpret_cast<LB_NetHeader *>(header_mem);
                                char *string_block = reinterpret_cast<char *>(header_array + count);
                                size_t offset = 0;
                                for (size_t i = 0; i < count; ++i) {
                                    const auto &entry = parsed_headers[i];
                                    header_array[i].name = string_block + offset;
                                    memcpy(string_block + offset, entry.first.c_str(), entry.first.size());
                                    offset += entry.first.size();
                                    string_block[offset++] = '\0';
                                    header_array[i].value = string_block + offset;
                                    memcpy(string_block + offset, entry.second.c_str(), entry.second.size());
                                    offset += entry.second.size();
                                    string_block[offset++] = '\0';
                                }
                                response.headers = header_array;
                                response.header_count = count;
                            } else {
                                response.error = LB_Error_OutOfMemory;
                                break;
                            }
                        }
                    }
                }
            }
        }

    } while (false);

    if (request) {
        WinHttpCloseHandle(request);
    }
    if (connect) {
        WinHttpCloseHandle(connect);
    }
    if (session) {
        WinHttpCloseHandle(session);
    }

    if (req->cancelled.load() || !req->callback) {
        free_response_buffers(response);
    }

    SetEvent(req->completion_event);

    if (!req->cancelled.load() && req->callback) {
        std::unique_ptr<NetResponsePayload> payload(new NetResponsePayload{});
        payload->response = response;
        payload->request = req;
        req->callback_scheduled.store(true);
        post_task_impl(&deliver_response, payload.release());
    } else {
        finalize_request(req);
    }

    return 0;
}
extern "C" LB_ErrorCode net_request_impl(const LB_NetRequestDesc *desc, LB_NetResponseCallback cb, void *ctx, lb_net_request **out_handle) {
    if (out_handle) {
        *out_handle = nullptr;
    }
    if (!desc || !desc->url_utf8 || !cb) {
        return LB_Error_BadArgument;
    }

    std::wstring url_w = utf8_to_wide(desc->url_utf8);
    if (url_w.empty()) {
        return LB_Error_BadArgument;
    }

    URL_COMPONENTSW components{};
    wchar_t host_buffer[256];
    wchar_t path_buffer[2048];
    components.dwStructSize = sizeof(components);
    components.lpszHostName = host_buffer;
    components.dwHostNameLength = static_cast<DWORD>(sizeof(host_buffer) / sizeof(host_buffer[0]));
    components.lpszUrlPath = path_buffer;
    components.dwUrlPathLength = static_cast<DWORD>(sizeof(path_buffer) / sizeof(path_buffer[0]));
    components.dwSchemeLength = (DWORD)-1;
    components.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(url_w.c_str(), 0, 0, &components)) {
        return LB_Error_BadArgument;
    }

    auto *req = new lb_net_request{};
    req->callback = cb;
    req->callback_ctx = ctx;
    req->completion_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!req->completion_event) {
        delete req;
        return LB_Error_Unknown;
    }

    req->host.assign(components.lpszHostName, components.dwHostNameLength);
    std::wstring path_part(components.lpszUrlPath, components.dwUrlPathLength);
    std::wstring extra_part(components.lpszExtraInfo ? components.lpszExtraInfo : L"",
                            components.dwExtraInfoLength == (DWORD)-1 ? 0 : components.dwExtraInfoLength);
    req->path_and_query = path_part + extra_part;
    if (req->path_and_query.empty()) {
        req->path_and_query = L"/";
    }
    req->port = components.nPort;
    req->secure = (components.nScheme == INTERNET_SCHEME_HTTPS);

    if (desc->method == LB_NetMethod_Custom && desc->custom_method) {
        req->method_w = utf8_to_wide(desc->custom_method);
    } else {
        switch (desc->method) {
            case LB_NetMethod_Post: req->method_w = L"POST"; break;
            case LB_NetMethod_Put: req->method_w = L"PUT"; break;
            case LB_NetMethod_Delete: req->method_w = L"DELETE"; break;
            case LB_NetMethod_Get:
            default: req->method_w = L"GET"; break;
        }
    }

    if (desc->headers && desc->header_count > 0) {
        for (size_t i = 0; i < desc->header_count; ++i) {
            std::wstring name_w = utf8_to_wide(desc->headers[i].name);
            std::wstring value_w = utf8_to_wide(desc->headers[i].value);
            req->headers.emplace_back(std::move(name_w), std::move(value_w));
        }
    }

    if (desc->body && desc->body_size > 0) {
        req->body.assign(desc->body, desc->body + desc->body_size);
    }

    unsigned thread_id = 0;
    HANDLE thread_handle = reinterpret_cast<HANDLE>(_beginthreadex(
        nullptr,
        0,
        &network_thread_proc,
        req,
        0,
        &thread_id));
    if (!thread_handle) {
        CloseHandle(req->completion_event);
        delete req;
        return LB_Error_Unknown;
    }
    req->thread_handle = thread_handle;

    if (out_handle) {
        *out_handle = req;
    }
    return LB_Error_Ok;
}

extern "C" void net_request_cancel_impl(lb_net_request *handle) {
    if (!handle) {
        return;
    }
    handle->cancelled.store(true);
    if (handle->completion_event) {
        WaitForSingleObject(handle->completion_event, INFINITE);
    }
    if (!handle->callback_scheduled.load()) {
        finalize_request(handle);
    }
}
