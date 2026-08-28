#include "session.h"
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace {
/// 单帧包体上限（1 MiB）：拒绝异常长度，防止恶意客户端诱导服务端无界申请堆内存（DoS）。
constexpr int MAX_PACKET_SIZE = 1024 * 1024;

/// 单次 recv 的栈缓冲区大小：一次最多读入 4 KiB，避免单帧过大时栈上分配过大。
constexpr int READ_BUFFER_SIZE = 4096;
} // namespace

/**
 * @brief 构造连接会话。
 */
Session::Session(int fd, const sockaddr_in& addr, FrameHandler handler)
    : m_fd(fd), m_addr(addr), m_handler(std::move(handler)) {}

/**
 * @brief 析构：关闭套接字（RAII 释放 fd）。
 */
Session::~Session() {
    if (m_fd >= 0) {
        close(m_fd);
    }
}

/**
 * @brief 从套接字读取并解析完整帧。
 * @return true 继续监听；false 关闭连接。
 * @note recv() 返回值语义：>0 读到字节；==0 对端优雅关闭（FIN）；<0 出错——
 *       EAGAIN/EWOULDBLOCK 表示非阻塞下暂无数据（读尽，本轮结束）；
 *       其它 errno（ECONNRESET 对端 RST、EINTR 等）统一按连接错误关闭。
 *       边缘触发下必须循环读到 EAGAIN，否则缓冲区残留的未读数据不会再触发 EPOLLIN。
 */
bool Session::handleRead() {
    // 边缘触发要求循环读到 EAGAIN，否则缓冲区里残留的未读数据不会再触发新事件。
    char buffer[READ_BUFFER_SIZE];
    while (true) {
        ssize_t n = recv(m_fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            m_readBuffer.insert(m_readBuffer.end(), buffer, buffer + n);
            if (!parseFrames()) {
                return false;
            }
        } else if (n == 0) {
            return false; // 对端关闭。
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true; // 本轮数据读完，等待下次 EPOLLIN。
            }
            return false; // 读取错误。
        }
    }
}

/**
 * @brief 从读缓冲解析完整帧。
 * @return false 表示非法包长，需关闭连接。
 * @note 字节序：包长为 4 字节本机小端 int（客户端与服务端同为 x86-64，直接 memcpy 解释）。
 *       粘包：一次 recv 可能包含多帧，循环解析直到缓冲不足；
 *       拆包：一帧可能分多次 recv 到达，缓冲不足时返回 true 等待下次 EPOLLIN 继续累积。
 */
bool Session::parseFrames() {
    while (true) {
        if (m_readingHeader) {
            // 长度字段不足 4 字节时继续累积。
            if (m_readBuffer.size() < sizeof(int)) {
                return true;
            }
            int packetSize = 0;
            std::memcpy(&packetSize, m_readBuffer.data(), sizeof(packetSize));
            if (packetSize <= 0 || packetSize > MAX_PACKET_SIZE) {
                return false; // 拒绝异常长度，防止无界申请。
            }
            m_expectSize = packetSize;
            m_readingHeader = false;
            m_readBuffer.erase(m_readBuffer.begin(), m_readBuffer.begin() + sizeof(int));
        } else {
            // 包体不足时继续累积。
            if (m_readBuffer.size() < static_cast<size_t>(m_expectSize)) {
                return true;
            }
            m_handler(m_fd, m_readBuffer.data(), m_expectSize);
            m_readBuffer.erase(m_readBuffer.begin(), m_readBuffer.begin() + m_expectSize);
            m_readingHeader = true;
        }
    }
}

/**
 * @brief 追加待发送字节。
 * @note 写缓冲为 std::string（堆内存，随内容增长）；enqueue 仅拷贝入缓冲，
 *       真正的网络发送在 handleWrite 中非阻塞执行，避免在事件循环里阻塞等写。
 */
bool Session::enqueueWrite(const char* data, int len) {
    m_writeBuffer.append(data, len);
    return true;
}

/**
 * @brief 非阻塞刷出写缓冲。
 * @return true 连接可用；false 需关闭。
 * @note send() 用 MSG_NOSIGNAL：对端关闭时返回 EPIPE 而非触发 SIGPIPE 终止进程；
 *       EAGAIN/EWOULDBLOCK 表示内核发送缓冲已满，剩余字节交给 EPOLLOUT 触发后续写。
 */
bool Session::handleWrite() {
    while (m_writeOffset < m_writeBuffer.size()) {
        ssize_t n =
            send(m_fd, m_writeBuffer.data() + m_writeOffset, m_writeBuffer.size() - m_writeOffset, MSG_NOSIGNAL);
        if (n > 0) {
            m_writeOffset += n;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true; // 内核发送缓冲已满，剩余字节等待 EPOLLOUT。
        } else {
            return false; // 发送错误（EPIPE/ECONNRESET 等）或对端关闭。
        }
    }
    m_writeBuffer.clear();
    m_writeOffset = 0;
    return true;
}
