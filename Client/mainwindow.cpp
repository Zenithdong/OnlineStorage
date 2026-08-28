#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>

namespace {
/**
 * @brief 把外部字符串安全拷贝进定长协议字段。
 * @tparam N 目标数组长度（含结尾 '\0'）。
 * @param dst 目标字符数组。
 * @param src 源 C 字符串。
 * @note snprintf 保证最多写入 N-1 字节并强制以 '\0' 结尾，替代 MSVC 的 strcpy_s，
 *       用于登录/注册/上传/搜索/删除/分享/提取/下载等所有协议组包。
 */
template <size_t N>
void CopyToArray(char (&dst)[N], const char* src) {
    std::snprintf(dst, N, "%s", src);
}
} // namespace

/**
 * @brief 构造主窗口：建立 Kernel 连接、显示登录窗口并注册全部响应信号。
 * @param parent 父窗口（默认顶层窗口）。
 * @note 信号槽统一用 Qt::QueuedConnection：Kernel 信号在接收线程发射，
 *       Qt 事件循环把它排入主线程队列后再调用槽，从而保证槽内可直接操作控件。
 *       若改用 DirectConnection，槽会在接收线程执行，操作控件将导致未定义行为。
 */
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);

    // 启动顺序：创建唯一 Kernel/TCP 连接 -> 显示登录窗口 -> 注册所有响应信号；登录前主窗口保持隐藏。
    m_kernel = std::make_unique<Kernel>();
    if (m_kernel->Connect()) {
        qDebug() << "open success!";
    } else {
        QMessageBox::critical(this, "result", "connect error");
    }
    m_login = std::make_unique<Login>(m_kernel.get());
    m_login->setWindowTitle("Login Windows");
    m_login->setWindowIcon(QIcon(":/icon.png"));
    m_login->show();

    // 明确使用队列连接，把接收线程发出的信号排入 Qt 主线程事件循环，避免后台线程直接操作控件。
    connect(m_kernel.get(), &Kernel::LoginRs, this, &MainWindow::LoginRs, Qt::QueuedConnection);
    connect(m_kernel.get(), &Kernel::RegisterRs, m_login.get(), &Login::RegisterRs, Qt::QueuedConnection);
    connect(m_kernel.get(), &Kernel::GetFileLisRs, this, &MainWindow::GetFileLisRs, Qt::QueuedConnection);
    connect(m_kernel.get(), &Kernel::UploadFileInfoRs, this, &MainWindow::UploadFileInfoRS, Qt::QueuedConnection);
    connect(m_kernel.get(), &Kernel::SelectFileRs, this, &MainWindow::SelectFileRs, Qt::QueuedConnection);
    connect(m_kernel.get(), &Kernel::ShareLinkRs, this, &MainWindow::ShareLinkRs, Qt::QueuedConnection);
    connect(m_kernel.get(), &Kernel::GetLinkRs, this, &MainWindow::GetLinkRs, Qt::QueuedConnection);
    connect(m_kernel.get(), &Kernel::DownLoadFileRs, this, &MainWindow::DownLoadFileRs, Qt::QueuedConnection);
}

/**
 * @brief 析构：先断开网络连接并等待接收线程退出，再释放子对象。
 * @note 顺序不能颠倒——先 DisConnect() 确保接收线程不再回调 Kernel，
 *       否则销毁 Kernel 后接收线程可能访问已释放对象。
 *       m_login/m_dialog/m_uploads 由 unique_ptr 自动释放；ui 按 Qt 约定手动 delete。
 */
MainWindow::~MainWindow() {
    // 先断开网络并等待接收线程退出，确保后续销毁窗口与上传上下文时不会再收到异步回调。
    if (m_kernel) {
        m_kernel->DisConnect();
    }
    // m_login/m_dialog/m_uploads 由 unique_ptr 持有，随对象析构自动释放；ui 仍由 Qt 约定手动释放。
    delete ui;
}

/**
 * @brief 首次显示时配置表格三列及伸缩模式。
 * @param event 显示事件。
 * @note 只做一次列初始化；登录成功显示主窗口时触发。
 */
void MainWindow::showEvent(QShowEvent* event) {
    // 文件列表固定展示名称、字节数和上传时间；Stretch 模式让三列随窗口宽度等分伸缩。
    ui->tableWidget->setColumnCount(3);
    QStringList lst;
    lst << "文件名" << "文件大小" << "上传时间";
    ui->tableWidget->setHorizontalHeaderLabels(lst);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    QMainWindow::showEvent(event);
}

/**
 * @brief 流式计算本地文件的 MD5 摘要。
 * @param file 本地文件路径。
 * @return 32 位小写十六进制摘要；文件不可读时返回空串。
 * @note 主线程执行。以二进制分块读取，换行不做平台转换；大文件耗时长会阻塞 UI。
 */
std::string MainWindow::FileDigest(const std::string& file) {
    // 以二进制模式分块读取，确保文本换行不会被平台转换；MD5 对每块增量计算并最终输出 32 位十六进制串。
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return "";

    MD5 md5;
    std::streamsize length;
    char buffer[1024];
    while (!in.eof()) {
        in.read(buffer, 1024);
        length = in.gcount();
        if (length > 0)
            md5.update(buffer, static_cast<size_t>(length));
    }
    in.close();
    return md5.toString();
}

/**
 * @brief 响应 Kernel::LoginRs，处理登录结果并切换窗口状态。
 * @param packet 服务端登录响应包。
 * @note 主线程执行、不阻塞。成功时保存 m_userId 并立即发送文件列表请求。
 */
void MainWindow::LoginRs(const QByteArray& packet) {
    // 登录结果决定窗口状态；只有成功时才保存用户 ID，并立即拉取该用户的网盘文件列表。
    m_login->StopLoginTimeout();
    const STRU_LOGIN_RS* sls = reinterpret_cast<const STRU_LOGIN_RS*>(packet.constData());
    if (sls->szResult == _login_res_failed) {
        QMessageBox::warning(this, "登录结果", "用户名或密码错误！");
    } else {
        if (sls->szResult == _login_res_noexist) {
            QMessageBox::information(this, "登陆结果", "用户不存在！");
        } else {
            // 用户 ID 是后续查询、上传、删除、分享和下载的业务主键，不能用界面用户名代替。
            m_login->hide();
            this->show();
            m_userId = sls->szUserId;

            STRU_GETFILELIST_RQ sgr;
            sgr.szUserId = m_userId;
            m_kernel->SendData(reinterpret_cast<char*>(&sgr), sizeof(sgr));
        }
    }
}

/**
 * @brief 响应 Kernel::GetFileLisRs，追加一页文件到表格。
 * @param packet 服务端文件列表响应包。
 * @note 主线程执行、不阻塞。同一次列表请求可能连续多包，按包追加行而非覆盖。
 */
void MainWindow::GetFileLisRs(const QByteArray& packet) {
    // 服务端每包最多返回 FILE_NUM 条；同一次列表请求可能连续触发多次该槽，因此按包追加行而不是覆盖。
    const STRU_GETFILELIST_RS* psgr = reinterpret_cast<const STRU_GETFILELIST_RS*>(packet.constData());
    for (int i = 0; i < psgr->szFileNum; i++) {
        int nRow = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(nRow);

        ui->tableWidget->setItem(nRow, 0, new QTableWidgetItem(QIcon(":/icon1.png"), psgr->arrFileInfo[i].szFileName));
        ui->tableWidget->setItem(nRow, 1, new QTableWidgetItem(QString::number(psgr->arrFileInfo[i].szFileSize)));
        ui->tableWidget->setItem(nRow, 2, new QTableWidgetItem(psgr->arrFileInfo[i].szFileUploadTime));
    }
}

/**
 * @brief 响应 Kernel::UploadFileInfoRs，按服务端策略决定是否补发文件正文。
 * @param packet 服务端上传协商响应包。
 * @note 主线程执行。普通/续传分支会读取本地文件并逐块发送，耗时与文件大小成正比。
 */
void MainWindow::UploadFileInfoRS(const QByteArray& packet) {
    const STRU_UPLOADFILEINFO_RS* sur = reinterpret_cast<const STRU_UPLOADFILEINFO_RS*>(packet.constData());

    // 元数据响应不携带本地路径，用 MD5 在待处理列表中关联原始文件；找不到表示任务已结束或响应无效。
    auto ite = std::find_if(m_uploads.begin(), m_uploads.end(),
                            [&](const auto& p) { return p->md5 == sur->szFileMD5; });
    if (ite == m_uploads.end()) {
        return;
    }
    uploadFileInfo* pInfo = ite->get();

    bool addToFileList = false;
    // 服务端依据 MD5、归属关系和磁盘进度返回四种策略，客户端只在普通上传或续传时发送正文。
    switch (sur->m_Result) {
    case _uploadfileinfo_repeat:
        QMessageBox::warning(this, "提示", "你已经上传过文件");
        break;

    case _uploadfileinfo_flashtrans:
        QMessageBox::information(this, "提示", "上传成功");
        addToFileList = true;
        break;

    case _uploadfileinfo_continue:
    case _uploadfileinfo_normal: {
        STRU_UPLOADFILECONTENT_RQ scr;
        scr.userid = m_userId;
        scr.fileid = sur->fileid;
        std::ifstream in{pInfo->filePath, std::ios::binary};
        if (!in) {
            QMessageBox::critical(this, "上传失败", "无法打开本地文件");
        } else {
            bool contentSent = true;
            // 续传必须先把源文件游标移动到服务端已落盘位置，后续每包固定携带 ONE_PAGE 缓冲区和有效字节数。
            if (sur->m_Result == _uploadfileinfo_continue) {
                in.seekg(sur->m_pos, std::ios::beg);
                if (!in) {
                    contentSent = false;
                }
            }
            while (contentSent) {
                in.read(scr.m_FileContent, sizeof(scr.m_FileContent));
                auto readNum = in.gcount();
                if (readNum > 0) {
                    scr.m_fileNum = static_cast<int32_t>(readNum);
                    if (!m_kernel->SendData(reinterpret_cast<char*>(&scr), sizeof(scr))) {
                        contentSent = false;
                    }
                } else {
                    // gcount() 为 0 时，eof() 为真表示正常读完，否则是读取错误。
                    contentSent = in.eof();
                    break;
                }
            }

            if (contentSent) {
                addToFileList = true;
            } else {
                QMessageBox::critical(this, "上传失败", "文件读取或发送中断，可重新上传以继续传输");
            }
        }
        break;
    }

    case _uploadfileinfo_failed:
    default:
        QMessageBox::critical(this, "上传失败", "服务端无法创建文件或写入文件信息");
        break;
    }

    if (addToFileList) {
        // 秒传或正文完整发送后即可在本地表格追加记录，无需再次请求整张列表。
        int nRow = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(nRow);
        QString strFileName = QFileInfo(QString::fromStdString(pInfo->filePath)).fileName();
        ui->tableWidget->setItem(nRow, 0, new QTableWidgetItem(QIcon(":/iconl.png"), strFileName));
        ui->tableWidget->setItem(nRow, 1, new QTableWidgetItem(QString::number(pInfo->fileSize)));
        ui->tableWidget->setItem(nRow, 2, new QTableWidgetItem(QString::fromStdString(pInfo->uploadTime)));
    }

    // 无论哪种策略都已消费该元数据响应，移除任务上下文即自动释放 unique_ptr 持有的对象。
    m_uploads.erase(ite);
}

/**
 * @brief 响应 Kernel::SelectFileRs，追加一页搜索结果到表格。
 * @param packet 服务端搜索响应包。
 * @note 主线程执行、不阻塞。搜索请求发送前已清空表格，这里按包追加。
 */
void MainWindow::SelectFileRs(const QByteArray& packet) {
    // 搜索请求发送前已经清空表格；服务端分页响应到达后逐包追加匹配文件。
    const STRU_SELECTFILE_RS* pssr = reinterpret_cast<const STRU_SELECTFILE_RS*>(packet.constData());
    for (int i = 0; i < pssr->szFileNum; i++) {
        int nRow = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(nRow);

        ui->tableWidget->setItem(nRow, 0, new QTableWidgetItem(QIcon(":/icon1.png"), pssr->arrFileInfo[i].szFileName));
        ui->tableWidget->setItem(nRow, 1, new QTableWidgetItem(QString::number(pssr->arrFileInfo[i].szFileSize)));
        ui->tableWidget->setItem(nRow, 2, new QTableWidgetItem(pssr->arrFileInfo[i].szFileUploadTime));
    }
}

/**
 * @brief 响应 Kernel::ShareLinkRs，展示分享提取码并提供一键复制。
 * @param packet 服务端分享响应包。
 * @note 主线程执行。模态对话框 exec() 会阻塞主窗口事件循环，直到用户关闭。
 */
void MainWindow::ShareLinkRs(const QByteArray& packet) {
    // 服务端为“用户-文件”分享关系生成或复用四位提取码，客户端提供一键复制以便转交他人。
    const STRU_SHARELINK_RS* psss = reinterpret_cast<const STRU_SHARELINK_RS*>(packet.constData());

    QString fileName = QString::fromLocal8Bit(psss->szFileName);
    QString code = QString::fromLocal8Bit(psss->szCode);

    if (code.isEmpty()) {
        QMessageBox::warning(this, "Share File", "分享失败，服务端没有生成提取码");
        return;
    }

    QString str = QString("File %1 shared successfully!\nExtraction code is: %2").arg(fileName).arg(code);

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Share File");
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText(str);

    QPushButton* copyBtn = msgBox.addButton("Copy Code", QMessageBox::ActionRole);
    msgBox.addButton("OK", QMessageBox::AcceptRole);

    msgBox.exec();
    if (msgBox.clickedButton() == copyBtn) {
        QApplication::clipboard()->setText(code);
    }
}

/**
 * @brief 响应 Kernel::GetLinkRs，把提取到的文件追加到表格。
 * @param packet 服务端提取响应包。
 * @note 主线程执行、不阻塞。
 */
void MainWindow::GetLinkRs(const QByteArray& packet) {
    // 提取成功意味着服务端已为当前用户增加文件映射和引用计数，因此可直接把文件展示到列表。
    const STRU_GETLINK_RS* psgs = reinterpret_cast<const STRU_GETLINK_RS*>(packet.constData());
    if (psgs->szResult == _getlink_failed) {
        QMessageBox::warning(this, "结果", "此文件您已拥有请不要重复提取");
    } else {
        int nRow = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(nRow);
        ui->tableWidget->setItem(nRow, 0, new QTableWidgetItem(QIcon(":/iconl.png"), psgs->szFileName));
        ui->tableWidget->setItem(nRow, 1, new QTableWidgetItem(QString::number(psgs->szFileSize)));
        ui->tableWidget->setItem(nRow, 2, new QTableWidgetItem(psgs->szFileUploadTime));
    }
}

/**
 * @brief 响应 Kernel::DownLoadFileRs，把一页下载内容写入本地文件。
 * @param packet 服务端下载响应包。
 * @note 主线程执行。每包按 m_downloadPos 定位写入并累计偏移；下载协议无结束标记，
 *       由服务端逐块发送直到文件末尾。
 */
void MainWindow::DownLoadFileRs(const QByteArray& packet) {
    const STRU_DOWNLOADFILE_RS* psds = reinterpret_cast<const STRU_DOWNLOADFILE_RS*>(packet.constData());

    if (psds->m_fileNum <= 0 || psds->m_fileNum > ONE_PAGE || m_downloadPath.empty()) {
        return;
    }

    // 下载响应按 ONE_PAGE 分块且没有单独的结束包；客户端按到达顺序写入，并用 m_downloadPos 维护下一次落盘偏移。
    std::fstream file{m_downloadPath, std::ios::in | std::ios::out | std::ios::binary};
    if (!file) {
        QMessageBox::critical(this, "下载失败", "无法打开下载目标文件");
        m_downloadPath.clear();
        m_downloadPos = 0;
        return;
    }
    file.seekp(m_downloadPos, std::ios::beg);
    if (!file) {
        QMessageBox::critical(this, "下载失败", "无法定位下载目标文件");
        m_downloadPath.clear();
        m_downloadPos = 0;
        return;
    }
    file.write(psds->m_FileContent, psds->m_fileNum);
    if (file) {
        m_downloadPos += psds->m_fileNum;
    } else {
        QMessageBox::critical(this, "下载失败", "写入下载文件失败");
        m_downloadPath.clear();
        m_downloadPos = 0;
    }
}

/**
 * @brief 响应「上传文件」动作，选择本地文件并发送上传元数据。
 * @note 主线程执行。选择文件后立即计算 MD5（可能耗时），随后组包发送元数据，
 *       并把本地上下文加入 m_uploads 等待服务端策略响应。
 */
void MainWindow::on_action_2_triggered() {
    // 上传分为两个阶段：先发送元数据询问服务端策略，收到响应后再决定是否传输文件正文。
    qDebug() << "上传文件被点击了";
    QString filePath = QFileDialog::getOpenFileName(
        this, tr("打开文件"), ".",
        tr("All Files(*.*);;Images (*.png *.xpm *.jpg);;Text files (*.txt);;XML files (*.xml)"));
    qDebug() << filePath.section('/', -1);
    QString fileName = filePath.section('/', -1);
    if (fileName == "")
        return;
    // QFile 只用于可靠取得 64 位文件大小，随后关闭，真正分块读取在上传策略响应槽中完成。
    QFile qfile(filePath);
    if (!qfile.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "上传失败", "无法读取所选文件");
        return;
    }
    qint64 fileSize = qfile.size();
    qfile.close();

    // 内容 MD5 同时承担服务端去重键和客户端异步任务关联键；计算失败时不能继续上传。
    std::string strMD5 = FileDigest(filePath.toStdString());
    if (strMD5.empty()) {
        QMessageBox::critical(this, "上传失败", "无法计算所选文件的校验值");
        return;
    }
    // 上传时间随元数据固化，并用于文件列表展示以及 user_file 映射的创建时间。
    QDateTime time = QDateTime::currentDateTime();
    QString strTime = time.toString("yyyy-MM-dd HH:mm:ss");
    qDebug() << strTime;

    QByteArray fileNameBytes = fileName.toUtf8();
    QByteArray filePathBytes = filePath.toUtf8();
    if (fileNameBytes.size() >= MAX_SIZE || filePathBytes.size() >= FILE_PATH) {
        QMessageBox::warning(this, "上传失败", "文件名或文件路径过长");
        return;
    }

    STRU_UPLOADFILEINFO_RQ sur;
    CopyToArray(sur.szFileMD5, strMD5.c_str());
    CopyToArray(sur.szFileName, fileNameBytes.constData());
    CopyToArray(sur.szFileUploadTime, strTime.toStdString().c_str());
    sur.szFilesize = fileSize;
    sur.UserId = m_userId;

    // 请求发出前把本地上下文加入等待列表，防止服务端快速响应时找不到对应任务。
    auto pInfo = std::make_unique<uploadFileInfo>();
    pInfo->fileSize = fileSize;
    pInfo->uploadTime = strTime.toStdString();
    pInfo->filePath = filePath.toStdString();
    pInfo->md5 = strMD5;
    pInfo->pos = 0;
    m_uploads.push_back(std::move(pInfo));

    if (!m_kernel->SendData(reinterpret_cast<char*>(&sur), sizeof(sur))) {
        m_uploads.pop_back();
        QMessageBox::critical(this, "上传失败", "文件信息发送失败");
    }
}

/**
 * @brief 响应「搜索」按钮，按关键字发送搜索请求。
 * @note 主线程执行、不阻塞。发送成功后再清空旧结果，失败则保留原表格。
 */
void MainWindow::on_Select_clicked() {
    // 搜索只作用于当前用户文件，服务端使用关键字执行模糊匹配；发送成功后再清空旧结果。
    STRU_SELECTFILE_RQ SSR;
    QByteArray keywordBytes = ui->lineEdit->text().toUtf8();
    if (keywordBytes.size() >= MAX_SIZE) {
        QMessageBox::warning(this, "搜索失败", "搜索内容过长");
        return;
    }
    CopyToArray(SSR.m_KeyWord, keywordBytes.constData());
    SSR.userid = m_userId;
    if (m_kernel->SendData(reinterpret_cast<char*>(&SSR), sizeof(SSR))) {
        ui->tableWidget->setRowCount(0);
    } else {
        QMessageBox::critical(this, "搜索失败", "请求发送失败");
    }
}

/**
 * @brief 响应「删除」按钮，发送删除请求并乐观移除选中行。
 * @note 主线程执行、不阻塞。界面先按发送成功移除，服务端负责引用计数与物理删除。
 */
void MainWindow::on_Delete_clicked() {
    // 客户端根据当前选中行发送“用户 ID + 文件名”；界面先按发送成功乐观移除，服务端负责引用计数。
    int nRow = ui->tableWidget->currentRow();
    if (nRow < 0) {
        return;
    }
    QTableWidgetItem* fileItem = ui->tableWidget->item(nRow, 0);
    if (!fileItem) {
        return;
    }
    QString fileName = fileItem->text();
    STRU_DELETEFILE_RQ sdr;
    sdr.userId = m_userId;
    CopyToArray(sdr.szFileName, fileName.toStdString().c_str());
    if (m_kernel->SendData(reinterpret_cast<char*>(&sdr), sizeof(sdr))) {
        QMessageBox::information(this, "Delete files", "Delete successfully!");
        ui->tableWidget->removeRow(nRow);
    }
}

/**
 * @brief 响应「分享」动作，为选中文件生成分享链接。
 * @note 主线程执行、不阻塞。服务端验证归属后创建或返回已有提取码。
 */
void MainWindow::on_action_triggered() {
    // 分享对象取自当前选中行，服务端验证文件归属后创建或返回已有提取码。
    int nRow = ui->tableWidget->currentRow();
    if (nRow < 0) {
        return;
    }
    QTableWidgetItem* fileItem = ui->tableWidget->item(nRow, 0);
    if (!fileItem) {
        return;
    }
    QString fileName = fileItem->text();
    STRU_SHARELINK_RQ ssr;
    ssr.userId = m_userId;
    CopyToArray(ssr.szFileName, fileName.toStdString().c_str());
    if (!m_kernel->SendData(reinterpret_cast<char*>(&ssr), sizeof(ssr))) {
        QMessageBox::critical(this, "Share File", "分享请求发送失败");
    }
}

/**
 * @brief 响应「提取」动作，弹提取码对话框并发送提取请求。
 * @note 主线程执行。模态对话框 exec() 阻塞主窗口事件循环；每次重新创建对话框避免沿用上次输入。
 */
void MainWindow::on_actionSend_File_triggered() {
    STRU_GETLINK_RQ sgq;
    // 每次重新创建模态对话框，避免沿用上一次输入；只有 Accepted 才进入提取业务。
    m_dialog = std::make_unique<Dialog>();
    int result = m_dialog->exec();
    if (result == QDialog::Accepted) {
        QByteArray codeBytes = QString::fromStdString(m_dialog->m_code).trimmed().toUtf8();
        if (codeBytes.isEmpty() || codeBytes.size() >= MAX_SIZE) {
            QMessageBox::warning(this, "提取失败", "请输入有效的提取码");
            return;
        }
        sgq.userId = m_userId;
        // 提取时间将成为当前用户与共享文件之间映射的创建时间。
        QDateTime time = QDateTime::currentDateTime();
        QString strTime = time.toString("yyyy-MM-dd HH:mm:ss");
        CopyToArray(sgq.szFileUploadTime, strTime.toStdString().c_str());
        CopyToArray(sgq.szCode, codeBytes.constData());
        if (!m_kernel->SendData(reinterpret_cast<char*>(&sgq), sizeof(sgq))) {
            QMessageBox::critical(this, "提取失败", "提取请求发送失败");
        }
    }
}

/**
 * @brief 响应「下载」动作，选择保存路径并发送下载请求。
 * @note 主线程执行、不阻塞。先创建/截断目标文件，再发送请求等待分块响应；
 *       服务端通过“用户 ID + 文件名”解析物理路径，客户端不直接指定服务器路径。
 */
void MainWindow::on_action_3_triggered() {
    STRU_DOWNLOADFILE_RQ sdr;

    // 下载开始前必须同时确定远端文件和本地保存路径；取消选择不会产生网络请求。
    int nRow = ui->tableWidget->currentRow();
    if (nRow == -1)
        return;
    m_downloadPath = QFileDialog::getSaveFileName(this, tr("下载文件"), "./newDownFile.png",
                                                  tr("Images (*.png *.xpm *.jpg);;All Files(*.*);;"
                                                     "Text files (*.txt);;XML files (*.xml)"))
                         .toStdString();
    if (m_downloadPath.empty()) {
        return;
    }
    m_downloadPos = 0;

    // 先以 trunc 创建或截断目标文件，随后每个响应块再用 in|out 按 m_downloadPos 定位写入。
    {
        std::ofstream out{m_downloadPath, std::ios::binary | std::ios::trunc};
        if (!out) {
            QMessageBox::critical(this, "下载失败", "无法创建下载目标文件");
            m_downloadPath.clear();
            return;
        }
    }

    // 请求只传用户 ID 和文件名；服务端从用户文件视图解析真实磁盘路径，避免客户端直接指定服务器路径。
    QTableWidgetItem* fileItem = ui->tableWidget->item(nRow, 0);
    if (!fileItem) {
        return;
    }
    std::string fileName = fileItem->text().toStdString();
    sdr.userId = m_userId;
    CopyToArray(sdr.szFileName, fileName.c_str());
    if (!m_kernel->SendData(reinterpret_cast<char*>(&sdr), sizeof(sdr))) {
        QMessageBox::critical(this, "下载失败", "下载请求发送失败");
        m_downloadPath.clear();
        m_downloadPos = 0;
    }
}
