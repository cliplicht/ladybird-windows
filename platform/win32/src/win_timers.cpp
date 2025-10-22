#include <windows.h>

#include "lb_platform.h"

struct TimerWrap {
    HANDLE hTimer{};
    void (*cb)(void*);
    void* ctx{};
    int repeat{};
    unsigned ms{};
};

static VOID CALLBACK TimerThunk(PVOID param, BOOLEAN /*fired*/) {
    auto* t = reinterpret_cast<TimerWrap*>(param);
    if (!t) return;
    if (t->cb) {
        t->cb(t->ctx);
    }
    if (!t->repeat) {
        DeleteTimerQueueTimer(nullptr, t->hTimer, nullptr);
        delete t;
    }
}

extern "C" void* timer_start_impl(unsigned ms, int repeat, void (*cb)(void*), void* ctx) {
    auto* t = new TimerWrap{};
    t->cb = cb; t->ctx = ctx; t->repeat = repeat; t->ms = ms;
    if (!CreateTimerQueueTimer(&t->hTimer, nullptr, TimerThunk, t, ms, repeat ? ms : 0, WT_EXECUTEDEFAULT)) {
        lbw_log("lb_platform: CreateTimerQueueTimer failed (err=%lu)", static_cast<unsigned long>(GetLastError()));
        delete t;
        return nullptr;
    }
    return t;
}

extern "C" void timer_stop_impl(void* handle) {
    auto* t = reinterpret_cast<TimerWrap*>(handle);
    if (!t) return;
    DeleteTimerQueueTimer(nullptr, t->hTimer, nullptr);
    delete t;
}
