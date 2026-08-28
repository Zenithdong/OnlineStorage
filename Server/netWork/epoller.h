#ifndef EPOLLER_H
#define EPOLLER_H
#include <sys/epoll.h>
#include <vector>

/**
 * @file epoller.h
 * @brief epoll 实例的 RAII 封装。
 *
 * 所属模块：服务端网络层（netWork）。
 * 依赖系统库：<sys/epoll.h>（Linux 专属）。
 *
 * 并发模型：本类仅被单线程 Reactor 使用，无内部锁；不同 Epoller 实例互不影响。
 *
 * 封装目的：把 epoll_create1 / epoll_ctl / epoll_wait 收敛为 add/mod/del/wait 四个接口，
 * 用 RAII 保证 epoll 描述符在对象析构时自动 close，避免手动关闭遗漏造成 fd 泄漏。
 *
 * 触发模式：采用边缘触发（EPOLLET），注册后只在 fd 状态由「不可读/写」变为「可读/写」时
 * 通知一次，因此使用方必须循环读写到 EAGAIN，否则残留数据不会再触发新事件（见 Session/Reactor）。
 */
class Epoller {
public:
    /// @brief 创建 epoll 实例；失败时 m_epfd 为 -1（后续 add/mod/del/wait 均静默失败）。
    Epoller();

    /// @brief 关闭 epoll 描述符（若创建成功）。
    ~Epoller();

    Epoller(const Epoller&) = delete;
    Epoller& operator=(const Epoller&) = delete;

    /**
     * @brief 向 epoll 注册 fd 及其关注事件。
     * @param fd 目标描述符。
     * @param events 关注事件位掩码（EPOLLIN/EPOLLOUT/EPOLLET 等）。
     * @param ptr 随事件回传的用户数据指针（本服务为 Session*，监听套接字传 nullptr）。
     * @return 是否成功。
     * @note 内部调用 epoll_ctl(EPOLL_CTL_ADD)；失败常见 errno：
     *       EBADF（fd 无效）、EEXIST（fd 已注册）、ENOMEM/ENOSPC（内核资源不足）。
     */
    bool add(int fd, uint32_t events, void* ptr);

    /**
     * @brief 修改 fd 的关注事件。
     * @param fd 目标描述符。
     * @param events 新事件位掩码。
     * @param ptr 新用户数据指针。
     * @return 是否成功。
     * @note 内部调用 epoll_ctl(EPOLL_CTL_MOD)；失败常见 errno：EBADF、ENOENT（fd 未注册）。
     */
    bool mod(int fd, uint32_t events, void* ptr);

    /**
     * @brief 从 epoll 移除 fd。
     * @param fd 目标描述符。
     * @return 是否成功。
     * @note 内部调用 epoll_ctl(EPOLL_CTL_DEL)；fd 关闭后内核会自动移除，显式 DEL 可避免竞态。
     */
    bool del(int fd);

    /**
     * @brief 等待就绪事件。
     * @param events 输出：就绪事件数组（调用方预分配容量）。
     * @param timeoutMs 阻塞超时（毫秒）。
     * @return 就绪事件数；0 表示超时；-1 表示错误（EINTR 被信号打断，调用方应重试）。
     * @note 内部调用 epoll_wait；单线程 Reactor 靠 1s 超时轮询退出标志。
     */
    int wait(std::vector<epoll_event>& events, int timeoutMs);

private:
    int m_epfd; ///< epoll 实例描述符；-1 表示 epoll_create1 失败。
};

#endif
