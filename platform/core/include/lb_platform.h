#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ABI version exported by the platform layer.
#define LB_PLATFORM_ABI_VERSION 1u

// Error codes returned by platform functions.
typedef enum LB_ErrorCode {
    LB_Error_Ok = 0,
    LB_Error_Unknown = 1,
    LB_Error_BadArgument = 2,
    LB_Error_NotSupported = 3,
    LB_Error_OutOfMemory = 4
} LB_ErrorCode;

typedef void (*lb_timer_cb)(void *);

struct lb_window;
typedef struct lb_window lb_window;

struct lb_net_request;
typedef struct lb_net_request lb_net_request;

typedef enum LB_ModifierFlags {
    LB_Mod_None   = 0,
    LB_Mod_Shift  = 1u << 0,
    LB_Mod_Ctrl   = 1u << 1,
    LB_Mod_Alt    = 1u << 2,
    LB_Mod_Super  = 1u << 3,
    LB_Mod_Caps   = 1u << 4,
    LB_Mod_Num    = 1u << 5
} LB_ModifierFlags;

typedef enum LB_MouseButton {
    LB_MouseButton_None   = 0,
    LB_MouseButton_Left   = 1,
    LB_MouseButton_Right  = 2,
    LB_MouseButton_Middle = 3,
    LB_MouseButton_X1     = 4,
    LB_MouseButton_X2     = 5
} LB_MouseButton;

typedef enum LB_EventType {
    LB_Event_None = 0,
    LB_Event_KeyDown,
    LB_Event_KeyUp,
    LB_Event_Text,
    LB_Event_MouseMove,
    LB_Event_MouseDown,
    LB_Event_MouseUp,
    LB_Event_MouseWheel,
    LB_Event_MouseLeave,
    LB_Event_WindowResize,
    LB_Event_WindowClose,
    LB_Event_WindowFocus,
    LB_Event_ImeStart,
    LB_Event_ImeComposition,
    LB_Event_ImeEnd,
    LB_Event_DropFiles
} LB_EventType;

typedef struct LB_KeyEvent {
    uint32_t virtual_key;
    uint32_t scan_code;
    uint32_t modifiers; // LB_ModifierFlags
    uint8_t is_repeat;
    uint8_t reserved[3];
} LB_KeyEvent;

typedef struct LB_TextEvent {
    uint32_t code_point; // UTF-32 scalar
    uint32_t modifiers;
} LB_TextEvent;

typedef struct LB_MouseEvent {
    int32_t x;
    int32_t y;
    int32_t delta_x;
    int32_t delta_y;
    int32_t wheel_delta_x;
    int32_t wheel_delta_y;
    uint32_t modifiers;
    LB_MouseButton button;
} LB_MouseEvent;

typedef struct LB_WindowResizeEvent {
    int32_t width;
    int32_t height;
    uint32_t dpi;
    float scale;
} LB_WindowResizeEvent;

typedef struct LB_WindowFocusEvent {
    uint8_t focused;
    uint8_t reserved[3];
} LB_WindowFocusEvent;

typedef struct LB_ImeEvent {
    const char *text_utf8;
    size_t length;
    uint32_t cursor_begin;
    uint32_t cursor_end;
} LB_ImeEvent;

typedef struct LB_DropEvent {
    const char *paths_utf8; // double-null terminated UTF-8 list
    size_t size;
    size_t count;
} LB_DropEvent;

typedef struct LB_Event {
    LB_EventType type;
    lb_window *window;
    union {
        LB_KeyEvent key;
        LB_TextEvent text;
        LB_MouseEvent mouse;
        LB_WindowResizeEvent resize;
        LB_WindowFocusEvent focus;
        LB_ImeEvent ime;
        LB_DropEvent drop;
    } data;
} LB_Event;

typedef void (*LB_EventCallback)(const LB_Event *event, void *ctx);

typedef struct LB_Buffer {
    uint8_t *data;
    size_t size;
} LB_Buffer;

typedef struct LB_FileResult {
    LB_Buffer buffer;
} LB_FileResult;

typedef struct LB_FileStat {
    uint64_t size;
    uint64_t modified_timestamp; // FILETIME (100-ns since 1601-01-01)
    uint32_t attributes;
    uint8_t is_directory;
    uint8_t reserved[3];
} LB_FileStat;

typedef struct LB_DirectoryListing {
    LB_Buffer entries; // double-null UTF-8 list
    size_t count;
} LB_DirectoryListing;

typedef enum LB_NetMethod {
    LB_NetMethod_Get = 0,
    LB_NetMethod_Post,
    LB_NetMethod_Put,
    LB_NetMethod_Delete,
    LB_NetMethod_Custom
} LB_NetMethod;

typedef struct LB_NetHeader {
    const char *name;   // UTF-8
    const char *value;  // UTF-8
} LB_NetHeader;

typedef struct LB_NetRequestDesc {
    LB_NetMethod method;
    const char *custom_method; // optional, used when method == LB_NetMethod_Custom
    const char *url_utf8;
    const LB_NetHeader *headers;
    size_t header_count;
    const uint8_t *body;
    size_t body_size;
    uint32_t flags;
} LB_NetRequestDesc;

typedef struct LB_NetResponse {
    LB_ErrorCode error;
    uint32_t http_status;
    const LB_NetHeader *headers;
    size_t header_count;
    LB_Buffer body;
} LB_NetResponse;

typedef void (*LB_NetResponseCallback)(const LB_NetResponse *response, void *ctx);

typedef struct LB_PlatformV1 {
    // Platform fills this with LB_PLATFORM_ABI_VERSION so the consumer can validate compatibility.
    uint32_t abi_version;

    // Initialize the platform backend. Called once before use.
    LB_ErrorCode (*init)(void);
    // Tear down the platform backend.
    void (*shutdown)(void);

    // Drive the platform event loop until quit_event_loop is invoked.
    void (*run_event_loop)(void);
    // Request the event loop to exit with the given code.
    void (*quit_event_loop)(int code);

    // Post an asynchronous task that will execute on the event loop thread (optional).
    void (*post_task)(void (*fn)(void *), void *ctx);

    // Create and destroy windows for presenting RGBA buffers.
    lb_window *(*win_create)(int w, int h, const char *title_utf8);
    void (*win_destroy)(lb_window *);

    // Register callback for input/events associated with the window.
    void (*win_set_event_callback)(lb_window *, LB_EventCallback, void *ctx);

    // Blit RGBA pixels to the specified window.
    LB_ErrorCode (*win_present_rgba8)(lb_window *, const void *pixels, int w, int h, int stride);

    // Timer helpers for scheduling callbacks on the event loop thread.
    // repeat != 0 keeps the timer firing every `ms` milliseconds.
    void *(*timer_start)(unsigned ms, int repeat, lb_timer_cb cb, void *ctx);
    void (*timer_stop)(void *handle);

    // File system helpers (UTF-8 paths).
    LB_ErrorCode (*fs_read_entire_file)(const char *path_utf8, LB_FileResult *out);
    LB_ErrorCode (*fs_write_entire_file)(const char *path_utf8, const void *data, size_t size);
    LB_ErrorCode (*fs_remove_file)(const char *path_utf8);
    LB_ErrorCode (*fs_stat)(const char *path_utf8, LB_FileStat *out);
    LB_ErrorCode (*fs_list_directory)(const char *path_utf8, LB_DirectoryListing *out);

    // Shared buffer release helper used for file and networking payloads.
    void (*buffer_free)(void *ptr);

    // Clipboard helpers (UTF-8 text only for now).
    LB_ErrorCode (*clipboard_write_text)(const char *text_utf8, size_t length);
    LB_ErrorCode (*clipboard_read_text)(LB_Buffer *out);

    // Networking helpers (WinHTTP-backed on Windows).
    LB_ErrorCode (*net_request)(const LB_NetRequestDesc *desc, LB_NetResponseCallback cb, void *ctx, lb_net_request **out_handle);
    void (*net_request_cancel)(lb_net_request *handle);
} LB_PlatformV1;

typedef LB_ErrorCode (*LB_QueryPlatformV1Fn)(LB_PlatformV1 *out);

// Simple logging helper (implemented by the platform backend).
void lbw_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
