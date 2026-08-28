#include "register.h"
#include "ui_register.h"
#include <algorithm>
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
 * @brief 构造注册窗口。
 * @param pKernel 共享业务接口。
 * @param parent 父窗口。
 */
Register::Register(IKernel* pKernel, QWidget* parent) : QWidget(parent), ui(new Ui::Register) {
    ui->setupUi(this);
    m_pKernel = pKernel;
    this->setWindowIcon(QIcon(":/icon.png"));
}

/**
 * @brief 析构：释放 uic 控件集。
 */
Register::~Register() {
    delete ui;
}

/**
 * @brief 响应「提交注册」按钮，校验输入并发送注册请求。
 * @note 主线程执行、不阻塞。依次校验：非空、长度、密码复杂度（大写/小写/数字各至少一个）、
 *       手机号范围；全部通过后组包发送，成功后发射 RegisterRequestSent。
 */
void Register::on_pushButton_clicked() {
    // 先规范化文本并转换为 UTF-8；字节长度才是定长协议字段真正需要校验的长度。
    QString UserName = ui->lineEdit->text().trimmed();
    QString PassWord = ui->lineEdit_2->text().trimmed();
    QString Phone = ui->lineEdit_3->text().trimmed();
    QByteArray userNameBytes = UserName.toUtf8();
    QByteArray passwordBytes = PassWord.toUtf8();

    if (userNameBytes.isEmpty()) {
        QMessageBox::warning(this, "错误", "用户名不能为空");
        ui->lineEdit->setFocus();
        return;
    }
    if (userNameBytes.size() >= MAX_SIZE || passwordBytes.size() >= MAX_SIZE) {
        QMessageBox::warning(this, "错误", "用户名或密码过长");
        return;
    }

    // 密码策略在客户端即时反馈：至少同时出现 ASCII 大写字母、小写字母和数字。
    bool Uppercase = std::any_of(PassWord.begin(), PassWord.end(), [](QChar c) { return c.isUpper(); });
    bool Lowercase = std::any_of(PassWord.begin(), PassWord.end(), [](QChar c) { return c.isLower(); });
    bool Number = std::any_of(PassWord.begin(), PassWord.end(), [](QChar c) { return c.isDigit(); });

    if (!(Uppercase && Lowercase && Number)) {
        QMessageBox::warning(this, "错误", "密码必须包含大写字母、小写字母和数字");
        ui->lineEdit_2->setFocus();
        return;
    }

    bool phoneOk = false;
    long long phoneNumber = Phone.toLongLong(&phoneOk);
    if (!phoneOk || phoneNumber <= 10000000000 || phoneNumber >= 19999999999) {
        QMessageBox::warning(this, "", "请输入合法的手机号");
        ui->lineEdit_3->setFocus();
        return;
    }

    // 业务字段写入零初始化的请求结构体；两端结构体布局必须一致，否则直接内存传输会产生字段错位。
    STRU_REGISTER_RQ srr;
    CopyToArray(srr.szName, userNameBytes.constData());
    CopyToArray(srr.szpassword, passwordBytes.constData());
    srr.szTel = phoneNumber;

    if (!m_pKernel->SendData(reinterpret_cast<char*>(&srr), sizeof(srr))) {
        QMessageBox::critical(this, "网络错误", "注册请求发送失败，请确认服务端正在运行");
        return;
    }
    emit RegisterRequestSent();
}
