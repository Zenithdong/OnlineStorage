#include "register.h"
#include "ui_register.h"

Register::Register(IKernel* pKernel, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Register)
{
    ui->setupUi(this);
    m_pKernel = pKernel;
    this->setWindowIcon(QIcon(":/icon.png"));
}

Register::~Register()
{
    delete ui;
}

void Register::on_pushButton_clicked()
{
    //采集页面上的信息
    QString UserName = ui->lineEdit->text().trimmed();
    QString PassWord = ui->lineEdit_2->text().trimmed();
    QString Phone = ui->lineEdit_3->text().trimmed();
    //格式校验
    bool Uppercase = false, Lowercase=false, Number=false;
    for(int i=0;i<PassWord.size();i++){
        if(PassWord[i] >= '0' && PassWord[i] <= '9'){
            //存在数字
            Number = true;
        }else if(PassWord[i] >='a' && PassWord[i] <='z'){
            //存在小写字母
            Lowercase = true;
        }else if(PassWord[i] >='A' && PassWord[i] <='Z'){
            //存在大写字母
            Uppercase = true;
        }
    }
    // 密码必须包含大写、小写和数字
    if (!(Uppercase && Lowercase && Number)) {
        QMessageBox::warning(this, "错误", "密码必须包含大写字母、小写字母和数字");
        ui->lineEdit_2->setFocus();
        return;
    }

    if(Phone.toLongLong() <= 10000000000 || Phone.toLongLong() >= 19999999999){
        QMessageBox::warning(this, "", "请输入合法的手机号");
        ui->lineEdit_3->setFocus();
        return;
    }
    //封装成STRU_REGISTER_RQ形式的信息
    STRU_REGISTER_RQ srr;

    strcpy_s(srr.szName, UserName.toStdString().c_str());
    strcpy_s(srr.szpassword, PassWord.toStdString().c_str());
    srr.szTel = Phone.toLongLong();
    //发送至服务器
    m_pKernel->SendData((char*)&srr, sizeof(srr));
}
