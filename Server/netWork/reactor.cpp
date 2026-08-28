#include "reactor.h"
#include "../Kernel/kernel.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

namespace {
constexpr int MAX_PACKET_SIZE = 1024 * 1024; ///< 单帧包体上限（1 MiB，防 DoS）。
constexpr int MAX_EVENTS = 64;               ///< 单次 epoll_wait 最多返回的事件数。
constexpr int EPOLL_TIMEOUT_MS = 1000;       ///< epoll_wait 超时（1s），用于轮询退出标志（避免引入 eventfd）。
} // namespace

/**
 * @brief 构造（不启动监听）。
 */
Reactor::Reactor() = default;

/**
 * @brief 析构：确保 Reactor 线程结束。
 */
Reactor::~Reactor() {
    UnitNetWork();
}

/**
 * @brief 创建监听套接字并启动 Reactor 线程。
 * @return 是否成功启动。
 * @note 系统调用错误处理：
 *       - socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) 失败返回 -1，errno=EMFILE/ENFILE（fd 耗尽）、ENOBUFS/ENOMEM；
 *       - bind 失败 errno=EADDRINUSE（端口被占）、EACCES（绑定特权端口<1024）、EADDRNOTAVAIL；
 *       - listen 失败 errno=EADDRINUSE、EBADF。
 *       SO_REUSEADDR 让 TIME_WAIT 状态的地址可立即复用，便于重启。
 */
bool Reactor::InitNetWork(unsigned long dwip, short nport) {
    if (m_listenFd >= 0) {
        return true;
    }

    m_listenFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenFd < 0) {
        std::cerr << "socket failed: " << strerror(errno) << std::endl;
        return false;
    }
    // SO_REUSEADDR 便于重启后立即复用地址。
    int reuse = 1;
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    setNonBlocking(m_listenFd);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(nport);
    serverAddr.sin_addr.s_addr = dwip;

    if (bind(m_listenFd, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
        std::cerr << "bind failed: " << strerror(errno) << std::endl;
        close(m_listenFd);
        m_listenFd = -1;
        return false;
    }
    if (listen(m_listenFd, SOMAXCONN) < 0) {
        std::cerr << "listen failed: " << strerror(errno) << std::endl;
        close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    // 监听套接字用 data.ptr == nullptr 标识，与客户端连接的 Session* 区分。
    m_epoller.add(m_listenFd, EPOLLIN | EPOLLET, nullptr);

    m_quitFlag = true;
    m_thread = std::thread(&Reactor::run, this);
    std::cout << "server listening on port " << nport << std::endl;
    return true;
}

/**
 * @brief 停止 Reactor 线程并释放所有套接字。
 * @note 顺序不可颠倒：先置退出标志，再 join；join 后线程已退出，才安全关闭
 *       client 与 listen 的 fd（线程内正在 epoll_wait 的 fd 若被其它线程 close 属未定义行为）。
 */
void Reactor::UnitNetWork() {
    // 置退出标志后，事件循环靠 1s 超时轮询退出；join 保证线程结束前不关闭还在使用的描述符。
    m_quitFlag = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        m_epoller.del(it->first);
    }
    m_sessions.clear();
    if (m_listenFd >= 0) {
        m_epoller.del(m_listenFd);
        close(m_listenFd);
        m_listenFd = -1;
    }
}

/**
 * @brief 向指定客户端发送完整业务包。
 * @return 是否入队成功。
 * @note 先入写缓冲「长度前缀 + 包体」，再立即非阻塞刷出；写不完的余量挂 EPOLLOUT。
 *       全程在 Reactor 线程内调用，无锁。
 */
bool Reactor::sendData(SOCKET sockWaiter, const char* szbuf, int nLen) {
    if (!szbuf || nLen <= 0 || nLen > MAX_PACKET_SIZE || sockWaiter < 0) {
        return false;
    }
    auto it = m_sessions.find(sockWaiter);
    if (it == m_sessions.end()) {
        return false;
    }
    Session* session = it->second.get();

    // 先入写缓冲长度前缀与包体，再立即尝试非阻塞刷出，写不完的余量交给 EPOLLOUT。
    if (!session->enqueueWrite(reinterpret_cast<const char*>(&nLen), sizeof(nLen)) ||
        !session->enqueueWrite(szbuf, nLen)) {
        closeSession(session);
        return false;
    }
    if (!session->handleWrite()) {
        closeSession(session);
        return false;
    }
    if (session->wantWrite()) {
        m_epoller.mod(sockWaiter, EPOLLIN | EPOLLOUT | EPOLLET, session);
    }
    return true;
}

/**
 * @brief INet 契约保留的空实现。
 */
void Reactor::recvData() {
    // epoll 版本由 run() 事件循环集中执行接收，该空实现仅用于履行 INet 契约。
}

/**
 * @brief 事件循环主体。
 */
void Reactor::run() {
    std::vector<epoll_event> events(MAX_EVENTS);
    while (m_quitFlag) {
        int n = m_epoller.wait(events, EPOLL_TIMEOUT_MS);
        if (n < 0) {
            if (errno == EINTR) {
                continue; // 被信号打断，继续等待。
            }
            break; // 其它错误（EBADF/EINVAL），退出循环。
        }
        for (int i = 0; i < n; i++) {
            if (events[i].data.ptr == nullptr) {
                handleAccept();
                continue;
            }
            Session* session = static_cast<Session*>(events[i].data.ptr);
            uint32_t ev = events[i].events;

            if (ev & (EPOLLERR | EPOLLHUP)) {
                closeSession(session);
                continue;
            }
            if (ev & EPOLLIN) {
                if (!session->handleRead()) {
                    closeSession(session);
                    continue;
                }
            }
            if (ev & EPOLLOUT) {
                if (!session->handleWrite()) {
                    closeSession(session);
                    continue;
                }
                if (!session->wantWrite()) {
                    // 写尽后摘除 EPOLLOUT，避免无数据可写时反复触发（减少无谓 epoll_wait 唤醒）。
                    m_epoller.mod(session->fd(), EPOLLIN | EPOLLET, session);
                }
            }
        }
    }
}

/**
 * @brief 接受新连接。
 * @note 边缘触发下必须 accept 到 EAGAIN，否则监听队列里剩余连接不会再触发事件；
 *       每个新连接设非阻塞、创建 Session、注册 EPOLLIN（ET）。
 */
void Reactor::handleAccept() {
    // 边缘触发下必须 accept 到 EAGAIN，否则监听队列里剩余连接不会再触发事件。
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);
        int clientFd = accept(m_listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (clientFd < 0) {
            return; // EAGAIN（已接受完）或错误（ECONNABORTED/EMFILE）都结束本轮 accept。
        }
        setNonBlocking(clientFd);

        auto session = std::make_unique<Session>(clientFd, clientAddr, [](int fd, const char* data, int len) {
            kernel::GetKernel().dealData(fd, data, len);
        });
        Session* raw = session.get();
        m_sessions.emplace(clientFd, std::move(session));
        m_epoller.add(clientFd, EPOLLIN | EPOLLET, raw);
    }
}

/**
 * @brief 关闭连接。
 * @note 先 epoll DEL、再 erase 触发 Session 析构 close(fd)，顺序保证 fd 在 epoll 中先被移除。
 */
void Reactor::closeSession(Session* session) {
    int fd = session->fd();
    m_epoller.del(fd);
    m_sessions.erase(fd); // Session 析构时 close(fd)。
}

/**
 * @brief 设置 fd 为非阻塞。
 * @note fcntl(F_GETFL) 读旧标志，再 F_SETFL 追加 O_NONBLOCK；失败静默（罕见，仅影响阻塞语义）。
 */
void Reactor::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
