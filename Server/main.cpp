#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <sys/select.h>
#include <unistd.h>
#include "./Kernel/kernel.h"

/**
 * @file main.cpp
 * @brief 服务端进程入口：初始化并监听，主线程等待回车或 Ctrl+C 后优雅退出。
 *
 * 进程/线程模型：单进程 + 两条线程——
 * - 主线程（本文件）：kernel::open() 初始化后，用 select 轮询 stdin 与退出标志，等待回车/信号；
 * - Reactor 线程：由 kernel::open() 内部（Reactor::InitNetWork）启动，独立处理 epoll 网络事件。
 *
 * 监听端口：8899（见 Reactor::InitNetWork）。
 * 协议：4 字节长度前缀 + 定长包体（见 packdef.h）。
 * 依赖系统库：<sys/select.h>（stdin 轮询）、<unistd.h>（read/STDIN_FILENO）、<csignal>（信号）。
 *
 * 信号处理：
 * - SIGINT/SIGTERM 用 std::signal 注册 OnSignal，处理器只置原子标志（异步信号安全，不调用
 *   malloc/printf 等非 async-signal-safe 函数）；主循环轮询该标志后调用 kernel::close() 优雅收尾；
 * - SIGPIPE 由网络层 send(MSG_NOSIGNAL) 抑制（见 session.cpp），无需在此捕获。
 */

namespace {
std::atomic<bool> g_stop{false}; ///< 跨线程退出开关：信号处理器置 true，主循环轮询。

/// stdin 轮询超时（100 微秒*1000=100ms）：平衡回车/信号的响应及时性与 CPU 空转开销。
constexpr long STDIN_POLL_TIMEOUT_US = 100000;

/**
 * @brief SIGINT/SIGTERM 处理器：只置原子标志。
 * @note 信号处理器上下文只能调用 async-signal-safe 操作；std::atomic<bool>::store(seq_cst)
 *       对 lock-free 类型是信号安全的，真正的收尾逻辑放到主循环里做。
 */
void OnSignal(int) {
    g_stop = true;
}
} // namespace

/**
 * @brief 进程入口。
 * @param argc 参数个数。
 * @param argv 参数数组。
 * @return 退出码（0 正常，1 启动失败）。
 */
int main() {
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    if (!kernel::GetKernel().open()) {
        std::cerr << "Server failed to start" << std::endl;
        return 1;
    }
    std::cout << "Server started successfully, listening on port 8899..." << std::endl;
    std::cout << "Press Enter or Ctrl+C to stop the server." << std::endl;

    // 100ms 超时轮询既保持对回车/Ctrl+C 的响应，又不会空转占用 CPU。
    // select 返回 >0 表示 stdin 可读（读到回车即退出）；返回 0 为超时（检查 g_stop）；
    // 返回 -1 为错误（EINTR 被信号打断，继续循环；其余错误忽略）。
    while (!g_stop) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        timeval timeout{0, STDIN_POLL_TIMEOUT_US};
        if (select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &timeout) > 0) {
            char c = 0;
            if (read(STDIN_FILENO, &c, 1) > 0) {
                break;
            }
        }
    }

    kernel::GetKernel().close();
    std::cout << "Server stopped." << std::endl;
    return 0;
}
