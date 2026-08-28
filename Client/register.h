#ifndef REGISTER_H
#define REGISTER_H

#include <QWidget>
#include "./Kernel/kernel.h"
#include <QMessageBox>

namespace Ui {
class Register;
}

/**
 * @brief 注册窗口：校验用户名、密码强度和手机号，并把合法数据封装成注册请求。
 *
 * 所属模块：客户端 UI 层。
 * 对应 .ui 文件：register.ui（uic 生成 Ui::Register）。
 *
 * UI 职责：收集三项输入并做客户端侧即时校验（必填、长度、密码复杂度、手机号范围），
 * 通过 IKernel 发送注册请求；请求成功写入套接字后发射 RegisterRequestSent 通知登录窗口。
 *
 * 线程模型：全部成员运行在 Qt 主线程。界面层只依赖 IKernel 抽象，不直接接触网络层，
 * 因此业务界面与底层 socket 实现解耦。
 */
class Register : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造注册窗口。
     * @param pKernel 外部共享的业务接口（仅借用，不拥有）。
     * @param parent 父窗口（默认为顶层窗口）。
     */
    explicit Register(IKernel* pKernel, QWidget* parent = nullptr);

    /// @brief 析构：释放 uic 控件集。
    ~Register();

private slots:
    /**
     * @brief 响应「提交注册」按钮，完成校验、组包发送并发射成功信号。
     * @note Qt Designer 按对象名自动连接；主线程执行、不阻塞。
     */
    void on_pushButton_clicked();

signals:
    /**
     * @brief 注册请求成功写入套接字后发射。
     * @note 主线程发射；Login 连接该信号以启动注册超时定时器。
     */
    void RegisterRequestSent();

private:
    Ui::Register* ui;   ///< uic 生成的控件集，父子关系归 this，析构手动 delete。
    IKernel* m_pKernel; ///< 主线程，外部共享的业务接口，仅借用、不释放。
};

#endif
