#include "login.h"
#include "ui_login.h"

Login::Login(IKernel*pKernel, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Login)
{
    ui->setupUi(this);
    m_pKernel = pKernel;
}

Login::~Login()
{
    delete ui;
}

void Login::on_pushButton_clicked()
{
    //采集页面上的信息
    QString UserName = ui->lineEdit->text();
    QString PassWord = ui->lineEdit_2->text();
    //封装成STRU_LOGIN_RQ形式的信息
    STRU_LOGIN_RQ slr;
    strcpy_s(slr.szName, UserName.toStdString().c_str());
    strcpy_s(slr.szpassword, PassWord.toStdString().c_str());
    //发送至服务器
    m_pKernel->SendData((char*)&slr, sizeof(slr));
}

void Login::on_pushButton_2_clicked()
{
    m_register = new Register(m_pKernel);
    this->hide();
    m_register->show();
}

void Login::RegisterRs(const char *szbuf)
{
    STRU_REGISTER_RS* srr = (STRU_REGISTER_RS*)szbuf;
    if(srr->szResult == _register_res_failed){
        QMessageBox::critical(this, "提示", "请输入正确的用户名或电话号：保持唯一属性");
    }
    else{
        QMessageBox::information(this, "提示", "注册成功，请返回继续登录");
        m_register->hide();
        this->show();
    }
}
