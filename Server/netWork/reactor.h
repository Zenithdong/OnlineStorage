#ifndef REACTOR_H
#define REACTOR_H
#include "INet.h"
#include "epoller.h"
#include "session.h"
#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>

/**
 * @file reactor.h
 * @brief 单线程 epoll Reactor：在一个线程内完成 accept / 读 / 写 / 业务处理。
 *
 * 所属模块：服务端网络层（netWork），INet 的实现。
 * 监听端口：8899（0.0.0.0，全本机 IPv4 地址）。
 * 协议：4 字节长度前缀 + 定长包体（见 packdef.h）。
 * 依赖系统库：<sys/epoll.h>（epoll）、<pthread>（std::thread）、<sys/socket.h>（socket/bind/listen/accept）。
 *
 * 并发模型：epoll 单线程（进程内仅一条 Reactor 线程 + 主线程控制）。
 * - Reactor 线程（run()）：处理 epoll 事件、调用 Session 收发、就地回调 kernel::dealData 执行业务；
 * - 主线程：调用 open/close（InitNetWork/UnitNetWork）控制启停；
 * - 所有共享状态（m_sessions、m_epoller、各 Session 收发缓冲）仅被 Reactor 线程访问，全程无锁；
 * - m_quitFlag 用 std::atomic_bool 跨线程可见：主线程置 false，Reactor 线程靠 1s 超时轮询退出。
 *
 * 取舍（TODO）：单线程下 MySQL 慢查询/大块磁盘 IO 会短暂阻塞事件循环、拖慢所有连接；
 * 换来无锁与实现简洁（教学/本地规模可接受）。未来若要恢复线程池，只需把
 * 「完整帧 -> handler」这一单一回调点改为投递任务队列，其余代码不动。
 */
class Reactor : public INet {
public:
    /// @brief 构造（不启动监听）。
    Reactor();

    /// @brief 析构：调用 UnitNetWork 确保 Reactor 线程结束。
    ~Reactor();

    /**
     * @brief 创建监听套接字并启动 Reactor 线程。
     * @param dwip 绑定的 IPv4 地址（网络字节序，0 表示 INADDR_ANY）。
     * @param nport 监听端口。
     * @return 是否成功启动。
     */
    bool InitNetWork(unsigned long dwip = 0, short nport = 8899) override;

    /**
     * @brief 停止 Reactor 线程并释放所有套接字。
     * @note 顺序：置退出标志 -> join（线程靠超时轮询退出）-> 关闭所有客户端与监听套接字。
     */
    void UnitNetWork() override;

    /**
     * @brief 向指定客户端发送一个完整业务包（INet 实现）。
     * @param sockWaiter 目标客户端 fd。
     * @param szbuf 包体首地址。
     * @param nLen 包体长度。
     * @return 是否入队成功。
     * @note 仅被 kernel 在 Reactor 线程内（handler 回调中）调用，因此可无锁访问 m_sessions；
     *       若未来从其它线程调用需为 m_sessions 加锁。
     */
    bool sendData(SOCKET sockWaiter, const char* szbuf, int nLen) override;

    /// @brief INet 契约保留；epoll 版本由 run() 事件循环集中执行接收。
    void recvData() override;

private:
    /**
     * @brief 事件循环主体（Reactor 线程）。
     * @note 边缘触发下：EPOLLIN 循环读到 EAGAIN、EPOLLOUT 循环写到 EAGAIN；
     *       listen fd 用 data.ptr==nullptr 标识，客户端连接用 Session* 标识。
     */
    void run();

    /**
     * @brief 接受新连接（边缘触发下 accept 到 EAGAIN）。
     * @note 为新连接设非阻塞、创建 Session 并注册 EPOLLIN。
     */
    void handleAccept();

    /**
     * @brief 关闭连接：从 epoll 移除并销毁 Session（Session 析构 close fd）。
     * @param session 待关闭的连接。
     */
    void closeSession(Session* session);

    /**
     * @brief 设置 fd 为非阻塞。
     * @param fd 目标描述符。
     * @note fcntl(F_GETFL/F_SETFL)；非阻塞是边缘触发与单线程事件循环的前提。
     */
    static void setNonBlocking(int fd);

    int m_listenFd = -1;                                ///< 监听套接字，-1 表示未启动。
    Epoller m_epoller;                                  ///< epoll 实例（RAII）。
    std::unordered_map<int, std::unique_ptr<Session>> m_sessions; ///< 堆内存：fd -> 连接，unique_ptr 独占。
    std::thread m_thread;                               ///< Reactor 线程，析构前 join。
    std::atomic_bool m_quitFlag{false};                 ///< 跨线程共享：退出开关，原子读写。
};

#endif
