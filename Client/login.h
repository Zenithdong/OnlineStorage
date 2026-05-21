#ifndef LOGIN_H
#define LOGIN_H

#include <QWidget>
#include "packdef.h"
#include "./Kernel/Kernel.h"
#include "register.h"

namespace Ui {
class Login;
}

class Login : public QWidget
{
    Q_OBJECT

public:
    explicit Login(IKernel* pKernel, QWidget *parent = nullptr);
    ~Login();

private slots:
    void on_pushButton_clicked();
    void on_pushButton_2_clicked();

public slots:
    void RegisterRs(const char* szbuf);

private:
    Ui::Login *ui;
    IKernel* m_pKernel;
    Register* m_register;
};

#endif // LOGIN_H
