#ifndef TCPNET_H
#define TCPNET_H
#pragma once
#include "INet.h"
#include "../Kernel/kernel.h"
#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>

/**
 * @brief Linux 专用 TCP 客户端：主线程发包，独立接收线程持续读取服务端响应。
 *
 * 所属模块：客户端网络层（Tcpnet 模块），INet 的具体实现。
 *
 * 线程模型（跨线程关键）：
 * - 主线程调用 ConnectServer / SendData / disConnectServer；
 * - 接收线程由 ThreadRecv 循环调用 RecvData，收满完整帧后回调 m_pKernel->DealData；
 * - 跨线程共享状态用两类机制保护：
 *   · m_socketMutex 串行化套接字的读、写、关闭，避免并发访问已失效描述符；
 *   · m_bRunning（std::atomic_bool）作为退出开关跨线程可见，无需加锁；
 * - 退出顺序必须是「清标志 -> 关闭套接字唤醒阻塞 recv -> join 线程」，
 *   否则接收线程可能永久阻塞在 recv 上。
 */
class tcpnet : public INet {
public:
    /**
     * @brief 构造客户端网络层。
     * @param pKernel 业务层回调接口（非拥有，收包后调用其 DealData）。
     */
    tcpnet(IKernel* pKernel);

    /// @brief 析构：调用 disConnectServer 确保接收线程结束。
    ~tcpnet();

public:
    /**
     * @brief 建立连接并创建接收线程。
     * @param szip 服务端地址。
     * @param nport 服务端端口。
     * @return 是否成功连接并启动线程；任一步失败都会回收已取得的资源。
     * @note 主线程调用。
     */
    bool ConnectServer(const char* szip = "127.0.0.1", short nport = 8899);

    /**
     * @brief 幂等关闭连接：清标志、关套接字、等待接收线程结束。
     * @note 主线程调用，可被析构或错误路径重复调用。
     */
    void disConnectServer();

    /**
     * @brief 在长度前缀之后发送业务包体，并通过 SendAll 处理 TCP 短写。
     * @param szbuf 包体首地址。
     * @param nLen 包体字节数。
     * @return 是否完整发送。
     * @note 主线程调用；锁内执行避免与关闭竞态。
     */
    bool SendData(const char* szbuf, int nLen);

    /**
     * @brief 阻塞读取一帧，校验包长后交给 IKernel::DealData。
     * @note 接收线程调用。
     */
    void RecvData();

    /**
     * @brief 接收线程静态入口。
     * @param self 指向 tcpnet 实例的上下文指针。
     * @note 由 std::thread 启动；受 m_bRunning 原子标志控制循环。
     */
    static void ThreadRecv(tcpnet* self);

private:
    /**
     * @brief 在锁内执行 shutdown 和 close，唤醒阻塞的 recv 并触发线程退出。
     * @note 主线程或接收线程都可调用；锁保证与 send/recv 互斥。
     */
    void CloseSocket();

    int m_sockClient;             ///< 跨线程共享：当前套接字，读写与关闭由 m_socketMutex 保护。
    std::atomic_bool m_bRunning;  ///< 跨线程共享：接收线程循环开关，原子读写、无需加锁。
    std::thread m_thread;         ///< 主线程持有：接收线程对象，析构前 join。
    IKernel* m_pKernel;           ///< 主线程传入：非拥有回调指针，把完整包交给客户端业务层。
    std::mutex m_socketMutex;     ///< 跨线程共享：串行化发送、读取句柄与关闭操作。
};

#endif
