#include "epoller.h"
#include <unistd.h>

/**
 * @brief 创建 epoll 实例。
 * @note epoll_create1(EPOLL_CLOEXEC)：CLOEXEC 使描述符在 exec 时自动关闭，防止子进程泄漏。
 *       参数 size 已废弃，传 0 即可；失败返回 -1 并设置 errno（EMFILE/ENFILE/ENOMEM）。
 */
Epoller::Epoller() : m_epfd(epoll_create1(EPOLL_CLOEXEC)) {}

/**
 * @brief 关闭 epoll 描述符。
 * @note close() 成功后内核自动释放 epoll 内部资源；已注册的 fd 不会被 close（仅解除监控）。
 */
Epoller::~Epoller() {
    if (m_epfd >= 0) {
        close(m_epfd);
    }
}

/**
 * @brief 注册 fd。
 */
bool Epoller::add(int fd, uint32_t events, void* ptr) {
    epoll_event ev{};
    ev.events = events;
    ev.data.ptr = ptr;
    return epoll_ctl(m_epfd, EPOLL_CTL_ADD, fd, &ev) == 0;
}

/**
 * @brief 修改 fd 的关注事件。
 */
bool Epoller::mod(int fd, uint32_t events, void* ptr) {
    epoll_event ev{};
    ev.events = events;
    ev.data.ptr = ptr;
    return epoll_ctl(m_epfd, EPOLL_CTL_MOD, fd, &ev) == 0;
}

/**
 * @brief 移除 fd 监控。
 */
bool Epoller::del(int fd) {
    return epoll_ctl(m_epfd, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

/**
 * @brief 等待就绪事件。
 */
int Epoller::wait(std::vector<epoll_event>& events, int timeoutMs) {
    return epoll_wait(m_epfd, events.data(), static_cast<int>(events.size()), timeoutMs);
}
