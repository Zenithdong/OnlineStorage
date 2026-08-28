#ifndef INET_H
#define INET_H
#include <sys/socket.h>

/**
 * @brief 客户端网络层抽象接口：统一约定连接生命周期和收发入口。
 *
 * 所属模块：客户端网络层（Tcpnet 模块）。
 *
 * 线协议：TCP 是字节流、没有业务消息边界，具体实现用「4 字节包长 + 定长包体」恢复完整帧；
 * 本接口不暴露任何网盘业务协议细节。
 */
class INet {
public:
    INet() {}
    virtual ~INet() {}

public:
    /**
     * @brief 连接指定服务端并启动后台接收线程。
     * @param szip 服务端地址。
     * @param nport 服务端端口。
     * @return 是否成功建立连接并启动接收线程。
     */
    virtual bool ConnectServer(const char* szip = "127.0.0.1", short nport = 8899) = 0;

    /**
     * @brief 中断阻塞 I/O、等待接收线程结束并释放套接字资源。
     * @note 幂等，可被析构或错误路径重复调用。
     */
    virtual void disConnectServer() = 0;

    /**
     * @brief 可靠发送一个完整业务包。
     * @param szbuf 包体首地址。
     * @param nLen 包体字节数。
     * @return 是否完整发送（实现必须处理 send() 短写）。
     */
    virtual bool SendData(const char* szbuf, int nLen) = 0;

    /**
     * @brief 接收一个带长度前缀的完整业务包并回调上层。
     * @note 由接收线程循环调用。
     */
    virtual void RecvData() = 0;
};

#endif
