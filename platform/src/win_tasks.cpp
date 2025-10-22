#include <windows.h>
#include <queue>
#include <mutex>

static const UINT WM_LBW_POST_TASK = WM_APP + 1;

struct Task {
    void (*fn)(void *);

    void *ctx;
};

static std::queue<Task> g_q;
static std::mutex g_m;

extern "C" void post_task_impl(void (*fn)(void *), void *ctx) { {
        std::lock_guard<std::mutex> lk(g_m);
        g_q.push(Task{fn, ctx});
    }

    // Wake up the main loop (post to the active queue thread)
    // We target the message queue of the foreground thread (current thread).
    PostThreadMessage(GetCurrentThreadId(), WM_LBW_POST_TASK, 0, 0);
}

static void pump_tasks() {
    for (;;) {
        Task t{}; {
            std::lock_guard<std::mutex> lk(g_m);
            if (g_q.empty()) break;
            t = g_q.front();
            g_q.pop();
        }
        if (t.fn) t.fn(t.ctx);
    }
}

extern "C" void lbw_pump_tasks_impl() { pump_tasks(); }
extern "C" UINT lbw_post_task_msg() { return WM_LBW_POST_TASK; }
