#ifndef DIALOG_H
#define DIALOG_H

#include <QDialog>
#include <string>
using namespace std;

namespace Ui {
class Dialog;
}

class Dialog : public QDialog
{
    Q_OBJECT

public:
    explicit Dialog(QWidget *parent = nullptr);
    ~Dialog();

private slots:
    void on_buttonBox_accepted();

public:
    string m_code;

private:
    Ui::Dialog *ui;
};

#endif // DIALOG_H
