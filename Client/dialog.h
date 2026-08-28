#ifndef DIALOG_H
#define DIALOG_H

#include <QDialog>
#include <string>

namespace Ui {
class Dialog;
}

/**
 * @brief 提取码输入对话框：只收集一次提取码输入，供主窗口发送提取请求。
 *
 * 所属模块：客户端 UI 层。
 * 对应 .ui 文件：dialog.ui（uic 生成 Ui::Dialog）。
 *
 * UI 职责：以模态方式（exec()）获取用户输入的分享提取码；确认后把原始文本保存到
 * m_code，由主窗口统一完成去空白、长度校验和协议组包，本类不做业务校验。
 *
 * 生命周期：由 MainWindow::m_dialog（unique_ptr<Dialog>）按需重建持有；
 * 由于每次提取都新建实例，其父对象为 nullptr，避免与 Qt 父窗口析构重复 delete。
 *
 * 线程模型：全部成员运行在 Qt 主线程。
 */
class Dialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief 构造提取码对话框。
     * @param parent 父窗口（默认 nullptr，无父对象，生命周期由调用方 unique_ptr 管理）。
     */
    explicit Dialog(QWidget* parent = nullptr);

    /// @brief 析构：释放 uic 控件集。
    ~Dialog();

private slots:
    /**
     * @brief 响应按钮盒的 Accepted 信号，把输入保存到 m_code。
     * @note Qt Designer 按对象名自动连接；主线程执行、不阻塞。仅保存原始输入，不做校验。
     */
    void on_buttonBox_accepted();

public:
    std::string m_code; ///< 主线程，本次确认的提取码原始文本，由主窗口在 exec() 返回后读取。

private:
    Ui::Dialog* ui; ///< uic 生成的控件集，父子关系归 this，析构手动 delete。
};

#endif
