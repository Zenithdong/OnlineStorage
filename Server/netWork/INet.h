#ifndef INET_H
#define INET_H
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// Linux 下套接字描述符就是 int；保留 SOCKET 别名以最小化业务层改动。
using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1; ///< 无效套接字哨兵值。
constexpr int SOCKET_ERROR = -1;      ///< 套接字操作失败返回值。

/**
 * @brief 服务端网络层抽象接口：只描述监听、关闭、发包和收包，不含任何网盘业务协议分支。
 *
 * 所属模块：服务端网络层（netWork）。
 * 依赖系统库：<sys/socket.h>、<netinet/in.h>、<arpa/inet.h>、<unistd.h>。
 *
 * 依赖倒置：业务层（kernel）只依赖本抽象，因此网络模型可在不改 kernel 的前提下替换
 * （select / epoll / io_uring）。当前实现为单线程 epoll Reactor。
 */
class INet {
public:
    INet() {}
    virtual ~INet() {}

public:
    /**
     * @brief 绑定地址端口、开始监听并启动内部 I/O 线程。
     * @param dwip 绑定地址（网络字节序，0 = INADDR_ANY）。
     * @param nport 监听端口。
     * @return 是否成功启动。
     */
    virtual bool InitNetWork(unsigned long dwip = 0, short nport = 8899) = 0;

    /**
     * @brief 停止 I/O 线程并释放监听套接字与客户端连接。
     */
    virtual void UnitNetWork() = 0;

    /**
     * @brief 向指定客户端发送「4 字节包长 + 业务包体」，并处理 TCP 短写。
     * @param sockWaiter 目标客户端 fd。
     * @param szbuf 包体首地址。
     * @param nLen 包体长度。
     * @return 是否成功入队。
     */
    virtual bool sendData(SOCKET sockWaiter, const char* szbuf, int nLen) = 0;

    /**
     * @brief 网络实现的接收入口；epoll 版本由 Reactor 事件循环集中执行。
     */
    virtual void recvData() = 0;
};

#endif
