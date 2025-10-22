#include <windows.h>
#include <string>
#include "lb_platform.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    HMODULE dll = LoadLibraryA("ladybird_platform_windows.dll");
    if (!dll) {
        MessageBoxA(nullptr, "DLL not found", "Error", MB_ICONERROR);
        return 1;
    }

    auto query = reinterpret_cast<LB_QueryPlatformV1Fn>(
        GetProcAddress(dll, "LB_QueryPlatformV1"));
    if (!query) {
        MessageBoxA(nullptr, "LB_QueryPlatformV1 missing", "Error", MB_ICONERROR);
        return 2;
    }

    LB_PlatformV1 plat{};
    if (query(&plat) != 0 || !plat.init) {
        MessageBoxA(nullptr, "Platform init failed", "Error", MB_ICONERROR);
        return 3;
    }
    if (plat.init() != 0) {
        MessageBoxA(nullptr, "Platform init() != 0", "Error", MB_ICONERROR);
        return 4;
    }

    auto *win = plat.win_create(800, 600, "Ladybird Windows — Layer PoC");

    // Dummy RGBA buffer used once for this PoC
    const int W = 800, H = 600, Stride = W * 4;
    static unsigned char pixels[800 * 600 * 4];
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int i = y * Stride + x * 4;
            pixels[i + 0] = (x ^ y) & 0xFF;
            pixels[i + 1] = (x * 2) & 0xFF;
            pixels[i + 2] = (y * 2) & 0xFF;
            pixels[i + 3] = 0xFF;
        }
    plat.win_present_rgba8(win, pixels, W, H, Stride);

    plat.run_event_loop();
    plat.shutdown();
    FreeLibrary(dll);
    return 0;
}
