#include "login.h"
#include "ui_login.h"
#include <cstddef>
#include <cstring>

namespace {
/**
 * @brief 把外部字符串安全拷贝进定长协议字段。
 * @tparam N 目标数组长度（含结尾 '\0'）。
 * @param dst 目标字符数组。
 * @param src 源 C 字符串。
 * @note strncpy 最多复制 N-1 字节后强制在末尾补 '\0'，替代 MSVC 的 strcpy_s。
 */
template <size_t N>
void CopyToArray(char (&dst)[N], const char* src) {
    std::strncpy(dst, src, N - 1);
    dst[N - 1] = '\0';
}
} // namespace

/**
 * @brief 构造登录窗口并配置两个超时定时器。
 * @param pKernel 共享业务接口。
 * @param parent 父窗口。
 * @note 定时器以 this 为父对象，随窗口销毁自动释放；两者均 singleShot，
 *       采用“发送请求后启动、收到响应后停止”的一次性模式。
 */
Login::Login(IKernel* pKernel, QWidget* parent)
    : QWidget(parent), ui(new Ui::Login), m_pKernel(pKernel), m_loginTimer(new QTimer(this)),
      m_registerTimer(new QTimer(this)) {
    ui->setupUi(this);

    // 登录和注册都采用“发送请求后启动、收到响应后停止”的单次计时器；计时器只负责用户提示，不取消网络请求。
    m_loginTimer->setSingleShot(true);
    connect(m_loginTimer, &QTimer::timeout, this,
            [this]() { QMessageBox::warning(this, "登录超时", "服务端在 5 秒内没有返回登录结果，请检查服务端状态"); });
    m_registerTimer->setSingleShot(true);
    connect(m_registerTimer, &QTimer::timeout, this, [this]() {
        QMessageBox::warning(m_register.get(), "注册超时", "服务端在 5 秒内没有返回注册结果，请检查服务端状态");
    });
}

/**
 * @brief 析构：释放 uic 控件集；m_register 由 unique_ptr 自动释放。
 */
Login::~Login() {
    delete ui;
}

/**
 * @brief 停止登录超时定时器。
 * @note 由 MainWindow 在登录响应到达时调用，防止成功后又弹超时。
 */
void Login::StopLoginTimeout() {
    m_loginTimer->stop();
}

/**
 * @brief 响应「登录」按钮，校验输入并发送登录请求。
 * @note 主线程执行、不阻塞。组包用协议结构体（构造时已设置类型字节），
 *       发送成功后启动 5 秒超时定时器。
 */
void Login::on_pushButton_clicked() {
    // 业务流程：读取输入 -> 在客户端完成必填和协议长度校验 -> 组装登录请求 -> 启动响应超时计时器。
    QString UserName = ui->lineEdit->text().trimmed();
    QString PassWord = ui->lineEdit_2->text();
    QByteArray userNameBytes = UserName.toUtf8();
    QByteArray passwordBytes = PassWord.toUtf8();

    if (userNameBytes.isEmpty() || passwordBytes.isEmpty()) {
        QMessageBox::warning(this, "登录", "用户名和密码不能为空");
        return;
    }
    if (userNameBytes.size() >= MAX_SIZE || passwordBytes.size() >= MAX_SIZE) {
        QMessageBox::warning(this, "登录", "用户名或密码过长");
        return;
    }

    // 协议结构体会被直接按内存发送，因此先构造以获得类型字段和零初始化的定长字符数组，再复制 UTF-8 数据。
    STRU_LOGIN_RQ slr;
    CopyToArray(slr.szName, userNameBytes.constData());
    CopyToArray(slr.szpassword, passwordBytes.constData());
    if (!m_pKernel->SendData(reinterpret_cast<char*>(&slr), sizeof(slr))) {
        QMessageBox::critical(this, "网络错误", "登录请求发送失败，请确认服务端正在运行");
        return;
    }
    m_loginTimer->start(5000);
}

/**
 * @brief 响应「注册」按钮，延迟创建并显示注册窗口。
 * @note 主线程执行。注册窗口复用同一个 IKernel；创建时把 RegisterRequestSent 信号
 *       连接到启动注册超时定时器的 lambda。
 */
void Login::on_pushButton_2_clicked() {
    // 注册窗口延迟创建并复用同一个 IKernel，保证登录与注册共用一条 TCP 连接。
    if (!m_register) {
        m_register = std::make_unique<Register>(m_pKernel);
        connect(m_register.get(), &Register::RegisterRequestSent, this, [this]() { m_registerTimer->start(5000); });
    }
    this->hide();
    m_register->show();
    m_register->raise();
    m_register->activateWindow();
}

/**
 * @brief 响应 Kernel::RegisterRs，处理注册结果并切换窗口。
 * @param packet 服务端注册响应包。
 * @note 经 QueuedConnection 在主线程执行。Kernel 已校验过包类型与大小，这里只解释业务结果。
 */
void Login::RegisterRs(const QByteArray& packet) {
    // Kernel 已按包类型和结构体大小校验过数据，这里只解释业务结果并切换窗口。
    m_registerTimer->stop();
    const STRU_REGISTER_RS* srr = reinterpret_cast<const STRU_REGISTER_RS*>(packet.constData());
    if (srr->szResult == _register_res_failed) {
        QMessageBox::critical(m_register.get(), "提示", "注册失败：用户名或手机号可能已存在");
    } else {
        QMessageBox::information(m_register.get(), "提示", "注册成功，请返回继续登录");
        m_register->hide();
        this->show();
    }
}
