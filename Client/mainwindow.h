#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDebug>
#include <QFileDialog>
#include <QDateTime>
#include <QMessageBox>
#include <QClipboard>
#include <QApplication>
#include <QPushButton>
#include <memory>
#include <string>
#include <vector>
#include "./Kernel/kernel.h"
#include "login.h"
#include "MD5/md5.h"
#include "dialog.h"
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

/**
 * @brief 客户端上传任务的临时上下文（非 QObject，仅主线程使用）。
 *
 * 上传分两阶段：先发送元数据询问服务端策略，收到响应后再读取本地文件正文。
 * 该结构在两次请求之间保存本地路径、时间、大小与 MD5，供响应槽关联并补发正文。
 *
 * 生命周期：由 MainWindow::m_uploads（std::vector<std::unique_ptr>）独占持有，
 * 从容器移除即自动析构，无需手动 delete。
 */
struct uploadFileInfo {
    std::string filePath;   ///< 本地绝对路径，仅在上传时打开源文件使用。
    std::string uploadTime; ///< 发起上传时生成的时间文本，成功后直接显示在表格。
    long long fileSize = 0; ///< 文件总字节数，用于展示并与服务端进度对应。
    long long pos = 0;      ///< 预留的本地传输偏移；实际续传位置以服务端响应为准。
    std::string md5;        ///< 任务关联键，用于从多个待响应上传中定位对应本地文件。
};

/**
 * @brief 网盘主窗口：登录后编排文件列表、搜索、上传、删除、分享、提取和下载等全部业务。
 *
 * 所属模块：客户端 UI 层。
 * 对应 .ui 文件：mainwindow.ui（uic 生成 Ui::MainWindow）。
 *
 * UI 职责：登录前隐藏自身，登录成功后显示并拉取当前用户的文件列表；表格展示文件
 * 名称/大小/上传时间三列，并提供搜索、上传、删除、分享、提取、下载六个动作入口。
 *
 * 线程模型：本类全部成员与槽函数都运行在 Qt 主线程。Kernel 的信号由接收线程产生，
 * 构造函数中用 Qt::QueuedConnection 连接，Qt 事件循环把信号排入主线程队列后再调用槽，
 * 因此所有槽函数都在主线程执行，可直接操作控件、无需加锁。
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /**
     * @brief 构造主窗口：建立 Kernel 连接、创建登录窗口并注册全部响应信号。
     * @param parent 父窗口（默认为顶层窗口，无父对象）。
     */
    explicit MainWindow(QWidget* parent = nullptr);

    /// @brief 析构：先断开网络连接等待接收线程退出，再释放各子对象。
    ~MainWindow() override;

    /**
     * @brief 首次显示时配置表格三列（文件名/大小/上传时间）及伸缩模式。
     * @param event 显示事件。
     * @note 保留基类调用以维持 Qt 默认行为；只做一次初始化。
     */
    void showEvent(QShowEvent* event);

    /**
     * @brief 流式读取本地文件并计算 MD5 摘要。
     * @param file 本地文件路径。
     * @return 32 位小写十六进制摘要；文件不可读时返回空串。
     * @note 主线程执行。大文件按 1 KB 分块读取，耗时与文件大小成正比，
     *       上传超大文件时界面会短暂无响应。
     */
    std::string FileDigest(const std::string& file);

private slots:
    /**
     * @brief 响应 Kernel::LoginRs 信号，处理登录结果。
     * @param packet 服务端登录响应包。
     * @note 主线程执行、不阻塞；成功时保存用户 ID 并立即拉取文件列表。
     */
    void LoginRs(const QByteArray& packet);

    /**
     * @brief 响应 Kernel::GetFileLisRs 信号，追加一页文件到表格。
     * @param packet 服务端文件列表响应包。
     * @note 主线程执行、不阻塞；同一请求可能连续多包，按包追加而非覆盖。
     */
    void GetFileLisRs(const QByteArray& packet);

    /**
     * @brief 响应 Kernel::UploadFileInfoRs 信号，按服务端策略决定是否发送正文。
     * @param packet 服务端上传协商响应包。
     * @note 主线程执行；普通/续传分支会读取本地文件并分块发送，可能耗时。
     */
    void UploadFileInfoRS(const QByteArray& packet);

    /**
     * @brief 响应 Kernel::SelectFileRs 信号，追加一页搜索结果到表格。
     * @param packet 服务端搜索响应包。
     * @note 主线程执行、不阻塞。
     */
    void SelectFileRs(const QByteArray& packet);

    /**
     * @brief 响应 Kernel::ShareLinkRs 信号，展示分享提取码并提供复制。
     * @param packet 服务端分享响应包。
     * @note 主线程执行；模态对话框会阻塞当前窗口的事件处理。
     */
    void ShareLinkRs(const QByteArray& packet);

    /**
     * @brief 响应 Kernel::GetLinkRs 信号，把提取到的文件追加到表格。
     * @param packet 服务端提取响应包。
     * @note 主线程执行、不阻塞。
     */
    void GetLinkRs(const QByteArray& packet);

    /**
     * @brief 响应 Kernel::DownLoadFileRs 信号，把一页下载内容写入本地文件。
     * @param packet 服务端下载响应包。
     * @note 主线程执行；每包按 m_downloadPos 定位写入，累计推进偏移。
     */
    void DownLoadFileRs(const QByteArray& packet);

    /**
     * @brief 响应「上传文件」动作（ui 的 action_2），选择本地文件并发送上传元数据。
     * @note Qt Designer 按对象名自动连接；主线程执行，含 MD5 计算可能耗时。
     */
    void on_action_2_triggered();

    /**
     * @brief 响应「搜索」按钮（Select），按关键字发送搜索请求。
     * @note 主线程执行、不阻塞；发送成功后再清空旧结果表格。
     */
    void on_Select_clicked();

    /**
     * @brief 响应「删除」按钮（Delete），发送删除请求并乐观移除选中行。
     * @note 主线程执行、不阻塞。
     */
    void on_Delete_clicked();

    /**
     * @brief 响应「分享」动作（action），为选中文件生成分享链接。
     * @note 主线程执行、不阻塞。
     */
    void on_action_triggered();

    /**
     * @brief 响应「提取」动作（actionSend_File），弹提取码对话框并发送提取请求。
     * @note 主线程执行；模态对话框 exec() 会阻塞主窗口事件循环。
     */
    void on_actionSend_File_triggered();

    /**
     * @brief 响应「下载」动作（action_3），选择保存路径并发送下载请求。
     * @note 主线程执行、不阻塞；先创建/截断目标文件，再等待分块响应。
     */
    void on_action_3_triggered();

private:
    Ui::MainWindow* ui;                          ///< uic 生成的控件集，父子关系归 this，析构中手动 delete。
    std::unique_ptr<Kernel> m_kernel;            ///< 主线程独占，客户端业务与网络入口。
    std::unique_ptr<Login> m_login;              ///< 主线程独占，登录/注册子窗口，与主窗口共享 Kernel。
    long long m_userId = 0;                      ///< 主线程，当前登录用户 ID，是所有文件业务的身份字段。
    std::vector<std::unique_ptr<uploadFileInfo>> m_uploads; ///< 主线程，等待上传策略的本地任务集合。
    std::unique_ptr<Dialog> m_dialog;            ///< 主线程，按需重建的提取码模态对话框。
    std::string m_downloadPath;                  ///< 主线程，当前下载目标路径；空串表示无活动下载。
    long long m_downloadPos = 0;                 ///< 主线程，当前下载已成功写入的字节偏移。
};
#endif
