#include <windows.h>
#include <queue>
#include <mutex>
#include <atomic>

static const UINT WM_LBW_POST_TASK = WM_APP + 1;

struct Task {
    void (*fn)(void *);
    void *ctx;
};

static std::queue<Task> g_q;
static std::mutex g_m;
static std::atomic<DWORD> g_event_thread_id{0};
static std::atomic<HWND> g_task_hwnd{nullptr};

extern "C" void lbw_register_event_thread(DWORD thread_id) {
    g_event_thread_id.store(thread_id, std::memory_order_relaxed);
    // Ensure the message queue exists for the event thread.
    if (thread_id == GetCurrentThreadId()) {
        MSG msg;
        PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE);
    }
}

extern "C" void lbw_set_task_hwnd(HWND hwnd) {
    g_task_hwnd.store(hwnd, std::memory_order_release);
}

extern "C" void lbw_clear_task_hwnd(HWND hwnd) {
    HWND expected = hwnd;
    g_task_hwnd.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
}

extern "C" void post_task_impl(void (*fn)(void *), void *ctx) {
    {
        std::lock_guard<std::mutex> lk(g_m);
        g_q.push(Task{fn, ctx});
    }

    if (HWND target_hwnd = g_task_hwnd.load(std::memory_order_acquire)) {
        SendMessage(target_hwnd, WM_LBW_POST_TASK, 0, 0);
        return;
    }

    DWORD target_thread = g_event_thread_id.load(std::memory_order_relaxed);
    if (!target_thread) {
        target_thread = GetCurrentThreadId();
    }
    PostThreadMessage(target_thread, WM_LBW_POST_TASK, 0, 0);
}

static void pump_tasks() {
    for (;;) {
        Task t{};
        {
            std::lock_guard<std::mutex> lk(g_m);
            if (g_q.empty()) break;
            t = g_q.front();
            g_q.pop();
        }
        if (t.fn) {
            t.fn(t.ctx);
        }
    }
}

extern "C" void lbw_pump_posted_tasks() { pump_tasks(); }
extern "C" UINT lbw_post_task_msg() { return WM_LBW_POST_TASK; }
