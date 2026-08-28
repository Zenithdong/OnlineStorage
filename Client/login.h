#ifndef LOGIN_H
#define LOGIN_H

#include <QWidget>
#include <QTimer>
#include <memory>
#include "packdef.h"
#include "./Kernel/kernel.h"
#include "register.h"

namespace Ui {
class Login;
}

/**
 * @brief 登录窗口：采集账号密码、发送登录请求，并管理注册子窗口的打开与注册结果展示。
 *
 * 所属模块：客户端 UI 层。
 * 对应 .ui 文件：login.ui（uic 生成 Ui::Login）。
 *
 * UI 职责：登录/注册两个按钮分别发起身份认证与打开注册窗口；两个 5 秒单次定时器
 * 为异步网络请求提供失败兜底提示（超时只提示、不取消底层请求）。
 *
 * 线程模型：全部成员运行在 Qt 主线程。RegisterRs 由 Kernel 的接收线程信号经
 * Qt::QueuedConnection 投递到主线程后调用，因此槽内可直接操作控件、无需加锁。
 */
class Login : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造登录窗口并配置两个超时定时器。
     * @param pKernel 主窗口传入的共享业务接口（仅借用，不拥有）。
     * @param parent 父窗口（默认为顶层窗口）。
     */
    explicit Login(IKernel* pKernel, QWidget* parent = nullptr);

    /// @brief 析构：释放 uic 控件集；m_register 由 unique_ptr 自动释放。
    ~Login();

    /**
     * @brief 停止登录超时定时器。
     * @note 登录响应到达后调用，避免成功登录后仍弹出超时提示。
     */
    void StopLoginTimeout();

private slots:
    /**
     * @brief 响应「登录」按钮，校验输入、组包发送登录请求并启动超时定时器。
     * @note Qt Designer 按对象名自动连接；主线程执行、不阻塞。
     */
    void on_pushButton_clicked();

    /**
     * @brief 响应「注册」按钮，延迟创建并显示注册窗口。
     * @note 主线程执行；注册窗口复用同一个 IKernel，保证登录与注册共用一条 TCP 连接。
     */
    void on_pushButton_2_clicked();

public slots:
    /**
     * @brief 响应 Kernel::RegisterRs 信号，处理注册结果并切换窗口。
     * @param packet 服务端注册响应包。
     * @note 经 QueuedConnection 在主线程执行、不阻塞；成功时关闭注册窗口并回到登录页。
     */
    void RegisterRs(const QByteArray& packet);

private:
    Ui::Login* ui;                     ///< uic 生成的控件集，父子关系归 this，析构手动 delete。
    IKernel* m_pKernel;                ///< 主线程，主窗口传入的共享业务接口，仅借用、不拥有。
    std::unique_ptr<Register> m_register; ///< 主线程，按需创建并复用的注册窗口，unique_ptr 独占。
    QTimer* m_loginTimer;              ///< 主线程，登录请求 5 秒单次超时定时器，父子关系归 this。
    QTimer* m_registerTimer;           ///< 主线程，注册请求 5 秒单次超时定时器，父子关系归 this。
};

#endif
