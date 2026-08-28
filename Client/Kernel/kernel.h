#ifndef KERNEL_H
#define KERNEL_H
#pragma once
#include <QByteArray>
#include <QObject>
#include <memory>
#include "IKernel.h"
#include "../Tcpnet/tcpnet.h"
#include "packdef.h"

/**
 * @brief 客户端协议分发中心：向下委托 INet 收发字节，向上用 Qt 信号发布业务响应。
 *
 * 所属模块：客户端业务层（Kernel 模块）。
 * 多重继承 QObject（信号发射）与 IKernel（业务接口），是网络层与 UI 层之间的桥。
 *
 * 线程模型（跨线程关键）：
 * - DealData() 在 tcpnet 的接收线程中被调用，内部按协议号发射对应信号；
 * - 信号参数 QByteArray 是深拷贝，保证跨线程投递时包体拥有独立生命周期；
 * - MainWindow 用 Qt::QueuedConnection 连接这些信号，Qt 事件循环把信号排入主线程队列，
 *   使槽函数在主线程执行，因此信号本身可在接收线程安全发射、无需加锁。
 */
class Kernel : public QObject, public IKernel {
    Q_OBJECT
public:
    /**
     * @brief 构造 Kernel 并创建网络层实现。
     * @param parent Qt 父对象（默认为 nullptr，生命周期由外部 unique_ptr 管理）。
     * @note 把 this 作为回调接口传给 tcpnet，形成“tcpnet 收完整包 -> Kernel 分发”的调用链。
     */
    explicit Kernel(QObject* parent = nullptr);

    /// @brief 析构：m_pNet 由 unique_ptr 自动释放，析构中会关闭连接并等待接收线程退出。
    ~Kernel() = default;

public:
    /**
     * @brief 建立到网盘服务端的 TCP 连接（IKernel 实现）。
     * @param szip 服务端地址，默认 127.0.0.1。
     * @param nport 服务端端口，默认 8899。
     * @return 连接是否成功。
     */
    bool Connect(const char* szip = "127.0.0.1", short nport = 8899);

    /**
     * @brief 断开连接并停止接收线程（IKernel 实现）。
     * @note 幂等；先清运行标志、关闭套接字唤醒阻塞 recv，再 join 线程。
     */
    void DisConnect();

    /**
     * @brief 发送一个完整业务包（IKernel 实现，网络层自动加 4 字节长度前缀）。
     * @param szbuf 业务结构体首地址。
     * @param Len 结构体字节数。
     * @return 是否成功写入套接字。
     */
    bool SendData(const char* szbuf, int Len);

    /**
     * @brief 接收线程取得完整包后调用的分发入口（IKernel 实现）。
     * @param szbuf 完整包缓冲区（接收线程临时分配，调用期间有效）。
     * @param len 包体长度。
     * @note 在接收线程执行。先按首字节路由，再用 sizeof 校验固定包长后发射信号，
     *       阻止短包被强制转换后越界读取。
     */
    void DealData(const char* szbuf, int len);

signals:
    /**
     * @brief 收到服务端登录响应后发射。
     * @param packet 响应包深拷贝。
     * @note 接收线程发射，经 QueuedConnection 由 MainWindow::LoginRs 在主线程处理。
     */
    void LoginRs(const QByteArray& packet);

    /**
     * @brief 收到服务端注册响应后发射。
     * @param packet 响应包深拷贝。
     * @note 接收线程发射，经 QueuedConnection 由 Login::RegisterRs 在主线程处理。
     */
    void RegisterRs(const QByteArray& packet);

    /**
     * @brief 收到服务端文件列表响应后发射。
     * @param packet 响应包深拷贝。
     * @note 接收线程发射，经 QueuedConnection 由 MainWindow::GetFileLisRs 在主线程处理。
     */
    void GetFileLisRs(const QByteArray& packet);

    /**
     * @brief 收到服务端上传协商响应后发射。
     * @param packet 响应包深拷贝。
     * @note 接收线程发射，经 QueuedConnection 由 MainWindow::UploadFileInfoRS 在主线程处理。
     */
    void UploadFileInfoRs(const QByteArray& packet);

    /**
     * @brief 收到服务端上传正文响应后发射（当前协议无此响应，保留扩展）。
     * @param packet 响应包深拷贝。
     */
    void UploadFileContentRs(const QByteArray& packet);

    /**
     * @brief 收到服务端搜索响应后发射。
     * @param packet 响应包深拷贝。
     * @note 接收线程发射，经 QueuedConnection 由 MainWindow::SelectFileRs 在主线程处理。
     */
    void SelectFileRs(const QByteArray& packet);

    /**
     * @brief 收到服务端分享响应后发射。
     * @param packet 响应包深拷贝。
     * @note 接收线程发射，经 QueuedConnection 由 MainWindow::ShareLinkRs 在主线程处理。
     */
    void ShareLinkRs(const QByteArray& packet);

    /**
     * @brief 收到服务端提取响应后发射。
     * @param packet 响应包深拷贝。
     * @note 接收线程发射，经 QueuedConnection 由 MainWindow::GetLinkRs 在主线程处理。
     */
    void GetLinkRs(const QByteArray& packet);

    /**
     * @brief 收到服务端下载响应后发射。
     * @param packet 响应包深拷贝。
     * @note 接收线程发射，经 QueuedConnection 由 MainWindow::DownLoadFileRs 在主线程处理。
     */
    void DownLoadFileRs(const QByteArray& packet);

private:
    std::unique_ptr<INet> m_pNet; ///< 实际指向 tcpnet，由 Kernel 独占；析构时关闭连接并 join 接收线程。
};

#endif
