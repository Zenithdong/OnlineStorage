#ifndef IKERNEL_H
#define IKERNEL_H
#include <sys/socket.h>

// Linux 下套接字描述符就是 int；保留 SOCKET 别名以最小化业务层改动。
using SOCKET = int;

/**
 * @brief 服务端核心层抽象：统一服务生命周期和请求入口。
 *
 * 所属模块：服务端业务层（Kernel）。
 *
 * 接口隔离：网络层只依赖本可回调契约，收包代码无需了解注册、上传或数据库细节；
 * 使网络层（select/epoll）与业务层（kernel）解耦。
 *
 * 线程约定：dealData 由 Reactor 线程调用（见 reactor.cpp handler 回调）；
 * open/close 由主线程调用。
 */
class IKernel {
public:
    IKernel() {}
    virtual ~IKernel() {}

public:
    /**
     * @brief 创建存储环境、连接数据库并开始监听。
     * @return 任一步失败返回 false。
     */
    virtual bool open() = 0;

    /**
     * @brief 停止网络线程、释放活动上传任务并断开数据库连接。
     */
    virtual void close() = 0;

    /**
     * @brief 接收完整业务包，按协议号、包长和字段合法性分发。
     * @param socketWaiter 来源客户端 fd，用于把结果回送原连接。
     * @param szbuf 完整包缓冲区（调用期间有效）。
     * @param len 包体长度。
     */
    virtual void dealData(SOCKET socketWaiter, const char* szbuf, int len) = 0;
};

#endif
