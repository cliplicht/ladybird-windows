#pragma once

#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>

#include "lb_platform.h"

struct lb_window {
    HWND hwnd{};
    BITMAPINFO bmi{};
    RECT client_rect{};
    UINT dpi{96};
    float scale{1.0f};
    int width{};
    int height{};
    const void *pixels{};
    int pixel_width{};
    int pixel_height{};
    int pixel_stride{};
    bool needs_present{};
    bool wants_close{};
    LB_EventCallback event_cb{};
    void *event_ctx{};
    bool tracking_mouse{};
    int last_x{};
    int last_y{};
    bool has_last_pos{};
    uint16_t pending_surrogate{};
    bool use_d3d{};
    Microsoft::WRL::ComPtr<ID3D11Device> d3d_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> d3d_swap_chain;
};

bool lbw_d3d_init(lb_window *win, int width, int height);
void lbw_d3d_destroy(lb_window *win);
bool lbw_d3d_resize(lb_window *win, int width, int height);
LB_ErrorCode lbw_d3d_present(lb_window *win, const void *pixels, int pw, int ph, int stride);
