#include "dialog.h"
#include "ui_dialog.h"

/**
 * @brief 构造提取码对话框。
 * @param parent 父窗口（默认 nullptr）。
 */
Dialog::Dialog(QWidget* parent) : QDialog(parent), ui(new Ui::Dialog) {
    ui->setupUi(this);
}

/**
 * @brief 析构：释放 uic 控件集。
 */
Dialog::~Dialog() {
    delete ui;
}

/**
 * @brief 响应按钮盒 Accepted，保存输入到 m_code。
 * @note 主线程执行、不阻塞。此处只保存原始输入，主窗口统一完成去空白、长度校验和协议组包。
 */
void Dialog::on_buttonBox_accepted() {
    // 此处只保存原始输入，主窗口统一完成去空白、长度校验和协议组包。
    m_code = ui->lineEdit->text().toStdString();
}
