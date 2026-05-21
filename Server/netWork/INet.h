#ifndef INET_H
#define INET_H
#include <winsock2.h>

// INet：网络层抽象接口
// 定义网络层必须实现的四个核心操作，便于后续替换不同网络实现（如 IOCP、epoll 移植等）
class INet {
public:
    INet() {}
    virtual ~INet() {}

public:
    // 初始化网络：绑定地址、开始监听、启动内部线程
    virtual bool InitNetWork(unsigned long dwip = 0, short nport = 8899) = 0;

    // 反初始化网络：停止线程、释放套接字资源
    virtual void UnitNetWork() = 0;

    // 向指定套接字发送数据（长度前缀协议）
    virtual bool sendData(SOCKET sockWaiter, const char* szbuf, int nLen) = 0;

    // 接收数据（阻塞式轮询，各实现可按需重写）
    virtual void recvData() = 0;
};

#endif // INET_H
