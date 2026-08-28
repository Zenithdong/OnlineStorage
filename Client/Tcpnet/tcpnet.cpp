#include "tcpnet.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {
/// 包长上限，拒绝异常长度防止不可信服务端诱导客户端申请过大堆内存。
constexpr int MAX_PACKET_SIZE = 1024 * 1024;

/**
 * @brief 完整发送指定长度数据，处理 send() 短写。
 * @param sock 套接字。
 * @param data 数据首地址。
 * @param length 字节数。
 * @return 是否全部进入系统发送缓冲。
 * @note send 可能只写出部分字节，循环推进偏移直到写完或失败。
 */
bool SendAll(int sock, const char* data, int length) {
    int offset = 0;
    while (offset < length) {
        int sent = send(sock, data + offset, length - offset, 0);
        if (sent <= 0) {
            return false;
        }
        offset += sent;
    }
    return true;
}

/**
 * @brief 完整接收指定长度数据，处理 recv() 短读。
 * @param sock 套接字。
 * @param data 接收缓冲区。
 * @param length 期望字节数。
 * @return 是否收满指定长度；断开、超时或错误统一视为失败。
 */
bool RecvAll(int sock, char* data, int length) {
    int offset = 0;
    while (offset < length) {
        int received = recv(sock, data + offset, length - offset, 0);
        if (received <= 0) {
            return false;
        }
        offset += received;
    }
    return true;
}
} // namespace

/**
 * @brief 构造客户端网络层。
 * @param pKernel 业务层回调接口。
 * @note 套接字初始为 -1（未连接），接收标志初始为 false。
 */
tcpnet::tcpnet(IKernel* pKernel) : m_sockClient(-1), m_bRunning(false), m_pKernel(pKernel) {}

/**
 * @brief 析构：确保接收线程结束、套接字关闭。
 */
tcpnet::~tcpnet() {
    disConnectServer();
}

/**
 * @brief 建立连接并创建接收线程。
 * @param szip 服务端地址。
 * @param nport 服务端端口。
 * @return 是否成功。
 * @note 主线程调用。Linux 无需 WSAStartup；inet_pton 线程安全且校验地址格式；
 *       设置写超时避免服务端失联时 GUI 线程在 send 上无限阻塞。
 */
bool tcpnet::ConnectServer(const char* szip, short nport) {
    // 已连接时直接成功返回，使重复初始化不会创建第二个套接字和接收线程。
    if (m_sockClient != -1) {
        return true;
    }

    // Linux 无需 WSAStartup，socket() 直接取得描述符，失败返回负值。
    m_sockClient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_sockClient < 0) {
        printf("socket failed: %s\n", strerror(errno));
        disConnectServer();
        return false;
    }

    sockaddr_in serverAddress = {};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(nport);
    // inet_pton 线程安全且支持点分十进制，成功返回 1，失败表示地址格式非法。
    if (inet_pton(AF_INET, szip, &serverAddress.sin_addr) != 1) {
        printf("inet_pton failed for address: %s\n", szip);
        disConnectServer();
        return false;
    }
    if (connect(m_sockClient, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0) {
        printf("connect failed: %s\n", strerror(errno));
        disConnectServer();
        return false;
    }

    // GUI 线程会直接调用发送函数，设置写超时可避免服务端失联时界面无限阻塞。
    timeval sendTimeout{5, 0};
    if (setsockopt(m_sockClient, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof(sendTimeout)) < 0) {
        printf("setsockopt(SO_SNDTIMEO) failed: %s\n", strerror(errno));
        disConnectServer();
        return false;
    }

    m_bRunning = true;
    m_thread = std::thread(&tcpnet::ThreadRecv, this);

    printf("Connected to server\n");
    return true;
}

/**
 * @brief 在锁内关闭套接字。
 * @note shutdown 先唤醒阻塞的 recv，再 close；锁防止与另一线程的 send 并发。
 */
void tcpnet::CloseSocket() {
    // shutdown 先唤醒阻塞的 recv，再关闭描述符；锁防止另一线程同时执行 send。
    std::scoped_lock lock(m_socketMutex);
    if (m_sockClient != -1) {
        shutdown(m_sockClient, SHUT_RDWR);
        close(m_sockClient);
        m_sockClient = -1;
    }
}

/**
 * @brief 断开连接并等待接收线程结束。
 * @note 主线程调用，幂等。顺序不可颠倒：先清标志、再关套接字唤醒 recv、最后 join，
 *       否则接收线程可能永久阻塞。
 */
void tcpnet::disConnectServer() {
    // 退出顺序必须是“清标志 -> 关套接字 -> 等线程”，否则可能永久等待阻塞接收。
    m_bRunning = false;
    CloseSocket();

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

/**
 * @brief 发送一个完整业务包。
 * @param szbuf 包体首地址。
 * @param nLen 包体字节数。
 * @return 是否成功。
 * @note 主线程调用。锁内检查套接字有效性后，依次发送长度前缀与包体；
 *       线协议采用本机 int32 字节序，客户端与服务端同为 Linux x86-64 才能直接解释。
 */
bool tcpnet::SendData(const char* szbuf, int nLen) {
    // 长度来自上层调用，发送前仍需做边界检查，避免无效指针或异常包进入网络层。
    if (!szbuf || nLen <= 0 || nLen > MAX_PACKET_SIZE) {
        return false;
    }

    bool sent = false;
    {
        std::scoped_lock lock(m_socketMutex);
        if (m_sockClient == -1) {
            return false;
        }
        // 线协议采用本机 int32 字节序；客户端和服务端同为 Linux x86-64 才能直接解释该前缀。
        sent = SendAll(m_sockClient, reinterpret_cast<const char*>(&nLen), sizeof(nLen)) &&
               SendAll(m_sockClient, szbuf, nLen);
    }

    if (!sent) {
        m_bRunning = false;
        CloseSocket();
        return false;
    }
    return true;
}

/**
 * @brief 阻塞读取一帧并回调业务层。
 * @note 接收线程调用。只在锁内复制套接字副本，不让阻塞 recv 长时间占锁，
 *       否则关闭连接将无法取得互斥锁。
 */
void tcpnet::RecvData() {
    // 只在锁内复制描述符，不让阻塞 recv 长时间占锁，否则关闭连接将无法取得互斥锁。
    int sock;
    {
        std::scoped_lock lock(m_socketMutex);
        sock = m_sockClient;
    }
    if (sock == -1) {
        m_bRunning = false;
        return;
    }

    int packetSize = 0;
    if (!RecvAll(sock, reinterpret_cast<char*>(&packetSize), sizeof(packetSize)) || packetSize <= 0 ||
        packetSize > MAX_PACKET_SIZE) {
        m_bRunning = false;
        CloseSocket();
        return;
    }

    // 使用 unique_ptr 管理包体，所有失败和成功路径都自动释放，无需配对 delete[]。
    auto packet = std::make_unique<char[]>(packetSize);
    if (!RecvAll(sock, packet.get(), packetSize)) {
        m_bRunning = false;
        CloseSocket();
        return;
    }

    // 回调仍发生在接收线程；Kernel 会复制为 QByteArray 并通过队列信号切回 Qt GUI 线程。
    m_pKernel->DealData(packet.get(), packetSize);
}

/**
 * @brief 接收线程入口：受原子标志控制循环收包。
 * @param self tcpnet 实例指针。
 * @note 静态入口不能直接访问成员，先把上下文还原为对象再循环调用 RecvData。
 */
void tcpnet::ThreadRecv(tcpnet* self) {
    // 静态入口不能直接访问成员，先把上下文指针还原为对象，再受原子标志控制循环。
    while (self->m_bRunning) {
        self->RecvData();
    }
}
