#ifndef IKERNEL_H
#define IKERNEL_H
#include <sys/socket.h>

/**
 * @brief 客户端业务层抽象接口：向界面提供连接/发送能力，向网络层提供完整包回调入口。
 *
 * 所属模块：客户端业务层（Kernel 模块）。
 *
 * 依赖倒置：QWidget 等 UI 只依赖本抽象，不接触具体 socket 实现，因此网络层可在
 * 不改界面的前提下替换（例如从 Winsock 换成 BSD socket）。
 *
 * 线程约定：Connect/SendData 由主线程调用；DealData 由网络层接收线程调用，
 * 实现方（Kernel）负责在 DealData 中把数据安全转发到主线程（通过 Qt 信号槽）。
 */
class IKernel {
public:
    IKernel() {}
    virtual ~IKernel() {}

public:
    /**
     * @brief 建立到网盘服务端的 TCP 连接。
     * @param szip 服务端地址。
     * @param nport 服务端端口。
     * @return 是否连接成功。
     */
    virtual bool Connect(const char* szip = "127.0.0.1", short nport = 8899) = 0;

    /**
     * @brief 停止接收线程并释放客户端套接字资源。
     */
    virtual void DisConnect() = 0;

    /**
     * @brief 发送一个业务结构体；网络实现自动添加 4 字节长度前缀。
     * @param szbuf 结构体首地址。
     * @param Len 结构体字节数。
     * @return 是否成功发送。
     */
    virtual bool SendData(const char* szbuf, int Len) = 0;

    /**
     * @brief 接收线程取得完整包后调用，由业务实现完成协议分发。
     * @param szbuf 完整包缓冲区。
     * @param len 包体长度。
     */
    virtual void DealData(const char* szbuf, int len) = 0;
};
#endif
