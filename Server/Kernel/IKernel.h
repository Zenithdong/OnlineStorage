#ifndef IKERNEL_H
#define IKERNEL_H
#include <winsock2.h>


// IKernel：核心业务层抽象接口
// 统一管理网络层与数据库层的生命周期，并提供数据处理入口
class IKernel {
public:
    IKernel() {}
    virtual ~IKernel() {}

public:
    // 启动服务：初始化网络层和数据库连接
    virtual bool open() = 0;

    // 关闭服务：断开网络监听和数据库连接
    virtual void close() = 0;

    // 处理客户端请求：根据数据包内容分发到对应业务逻辑
    // socketWaiter：发送请求的客户端套接字，szbuf：接收到的原始数据
    virtual void dealData(SOCKET socketWaiter, const char* szbuf) = 0;
};

#endif // IKERNEL_H
