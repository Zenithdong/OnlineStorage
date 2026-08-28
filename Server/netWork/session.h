#ifndef SESSION_H
#define SESSION_H
#include <cstddef>
#include <functional>
#include <netinet/in.h>
#include <string>
#include <vector>

/**
 * @file session.h
 * @brief 每个客户端连接对应的 Session：负责「4 字节包长 + 包体」解帧与收发缓冲。
 *
 * 所属模块：服务端网络层（netWork）。
 * 依赖系统库：<netinet/in.h>（sockaddr_in）、<sys/socket.h>（recv/send）。
 *
 * 协议：长度前缀报文——先 4 字节本机小端 int 表示包体长度，后接定长包体；
 * TCP 是字节流，必须用读缓冲累积半包、逐帧切分，正确处理粘包（一读多帧）与拆包（一帧多读）。
 *
 * 生命周期：由 Reactor 的 unordered_map<int, unique_ptr<Session>> 独占持有，
 * 析构时 close(fd)，实现 fd 的 RAII 释放。
 *
 * 线程归属：所有成员仅被单线程 Reactor 访问，无锁。
 */
class Session {
public:
    /// 解析出完整帧后的回调（本服务传入 kernel::dealData）；std::function 每次回调有堆分配开销，
    /// 但业务路径非热路径、可接受（TODO：若未来追求零分配可改为裸函数指针 + void* 上下文）。
    using FrameHandler = std::function<void(int fd, const char* data, int len)>;

    /**
     * @brief 构造连接会话。
     * @param fd 已 accept 的客户端套接字（须已设为非阻塞）。
     * @param addr 对端地址。
     * @param handler 完整帧回调。
     */
    Session(int fd, const sockaddr_in& addr, FrameHandler handler);

    /// @brief 析构时关闭套接字（RAII）。
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    int fd() const {
        return m_fd;
    }
    const sockaddr_in& addr() const {
        return m_addr;
    }

    /**
     * @brief 从套接字读取并累积到读缓冲，解析完整帧。
     * @return true 表示继续监听；false 表示连接需关闭。
     * @note 边缘触发下内部循环 recv 直到 EAGAIN；对端关闭返回 0、错误返回 -1（errno 见实现）。
     */
    bool handleRead();

    /**
     * @brief 追加待发送字节（先写长度前缀，再写包体）。
     * @param data 数据首地址。
     * @param len 数据长度。
     * @return 恒为 true（写缓冲为 std::string，失败为内存不足异常，由上层兜底）。
     */
    bool enqueueWrite(const char* data, int len);

    /**
     * @brief 非阻塞刷出写缓冲。
     * @return true 表示连接仍可用（可能未写完，需挂 EPOLLOUT 继续）；false 表示需关闭。
     */
    bool handleWrite();

    /// @return 是否还有未写完的字节（用于决定是否注册 EPOLLOUT）。
    bool wantWrite() const {
        return m_writeOffset < m_writeBuffer.size();
    }

private:
    /**
     * @brief 从读缓冲尽可能解析完整帧。
     * @return false 表示收到非法包长（<=0 或超过上限），需关闭连接。
     * @note 状态机：READ_HEADER(4B) -> READ_BODY(len) -> 回调 -> 回到 READ_HEADER。
     */
    bool parseFrames();

    int m_fd;                   ///< 客户端套接字，析构时 close。
    sockaddr_in m_addr;         ///< 对端地址（栈内值，仅用于日志/排查）。
    FrameHandler m_handler;     ///< 完整帧回调。

    std::vector<char> m_readBuffer; ///< 堆内存：累积尚未处理完的字节（半包缓冲）。
    int m_expectSize = 0;           ///< READ_BODY 阶段期望的包体长度。
    bool m_readingHeader = true;    ///< 状态机阶段：true 读 4 字节包长，false 读包体。

    std::string m_writeBuffer;      ///< 堆内存：待发送字节（长度前缀 + 包体）。
    size_t m_writeOffset = 0;       ///< 已写出的字节偏移。
};

#endif
