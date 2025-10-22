#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <cstring>

#include "win_window_internal.h"

using Microsoft::WRL::ComPtr;

static bool create_swap_chain(lb_window *win, int width, int height, ComPtr<IDXGISwapChain1> &swap_chain1) {
    ComPtr<IDXGIDevice> dxgi_device;
    if (FAILED(win->d3d_device.As(&dxgi_device))) {
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgi_device->GetAdapter(adapter.GetAddressOf()))) {
        return false;
    }

    ComPtr<IDXGIFactory2> factory2;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(factory2.GetAddressOf())))) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width > 0 ? width : 1;
    desc.Height = height > 0 ? height : 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = 0;

    HRESULT hr = factory2->CreateSwapChainForHwnd(
        win->d3d_device.Get(),
        win->hwnd,
        &desc,
        nullptr,
        nullptr,
        swap_chain1.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    factory2->MakeWindowAssociation(win->hwnd, DXGI_MWA_NO_ALT_ENTER);
    return true;
}

bool lbw_d3d_init(lb_window *win, int width, int height) {
    if (!win) return false;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL obtained{};

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        levels,
        static_cast<UINT>(sizeof(levels) / sizeof(levels[0])),
        D3D11_SDK_VERSION,
        win->d3d_device.GetAddressOf(),
        &obtained,
        win->d3d_context.GetAddressOf());

    if (FAILED(hr)) {
        // Try WARP as fallback
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            flags,
            levels,
            static_cast<UINT>(sizeof(levels) / sizeof(levels[0])),
            D3D11_SDK_VERSION,
            win->d3d_device.GetAddressOf(),
            &obtained,
            win->d3d_context.GetAddressOf());
    }

    if (FAILED(hr)) {
        win->d3d_device.Reset();
        win->d3d_context.Reset();
        return false;
    }

    ComPtr<IDXGISwapChain1> swap_chain1;
    if (!create_swap_chain(win, width, height, swap_chain1)) {
        win->d3d_context.Reset();
        win->d3d_device.Reset();
        return false;
    }

    if (FAILED(swap_chain1.As(&win->d3d_swap_chain))) {
        lbw_d3d_destroy(win);
        return false;
    }

    win->use_d3d = true;
    return true;
}

void lbw_d3d_destroy(lb_window *win) {
    if (!win) return;
    win->d3d_context.Reset();
    win->d3d_swap_chain.Reset();
    win->d3d_device.Reset();
    win->use_d3d = false;
}

bool lbw_d3d_resize(lb_window *win, int width, int height) {
    if (!win || !win->use_d3d || !win->d3d_swap_chain) return false;
    width = width > 0 ? width : 1;
    height = height > 0 ? height : 1;
    if (win->d3d_context) {
        win->d3d_context->ClearState();
    }
    HRESULT hr = win->d3d_swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    if (FAILED(hr)) {
        lbw_d3d_destroy(win);
        return false;
    }
    return true;
}

LB_ErrorCode lbw_d3d_present(lb_window *win, const void *pixels, int pw, int ph, int stride) {
    if (!win || !win->use_d3d || !win->d3d_swap_chain || !win->d3d_context) {
        return LB_Error_NotSupported;
    }
    if (pw <= 0 || ph <= 0 || stride <= 0) {
        return LB_Error_BadArgument;
    }
    if (stride < pw * 4) {
        return LB_Error_BadArgument;
    }
    if (pw != win->width || ph != win->height) {
        return LB_Error_NotSupported;
    }

    ComPtr<ID3D11Texture2D> back_buffer;
    HRESULT hr = win->d3d_swap_chain->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf()));
    if (FAILED(hr)) {
        return LB_Error_Unknown;
    }

    D3D11_TEXTURE2D_DESC desc;
    back_buffer->GetDesc(&desc);
    if (static_cast<int>(desc.Width) != pw || static_cast<int>(desc.Height) != ph) {
        return LB_Error_NotSupported;
    }

    win->d3d_context->UpdateSubresource(back_buffer.Get(), 0, nullptr, pixels, stride, 0);
    hr = win->d3d_swap_chain->Present(1, 0);
    if (FAILED(hr)) {
        return LB_Error_Unknown;
    }
    return LB_Error_Ok;
}
