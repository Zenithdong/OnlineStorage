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
    virtual bool Connect(const char * szip = "127.0.0.1",short nport = 8899) = 0;
    virtual void DisConnect() = 0;
    virtual bool SendData(const char* szbuf, int Len) = 0;
    virtual void DealData(const char* szbuf) = 0;
};
#endif // IKERNEL_H
