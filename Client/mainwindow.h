#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <./Kernel/kernel.h>
#include <QDebug>
#include "login.h"
#include <QFileDialog>
#include "MD5/md5.h"
#include <QDateTime>
#include <QMessageBox>
#include <QClipboard>
#include <QApplication>
#include <QPushButton>
#include "dialog.h"
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

struct uploadFileInfo {
    char szFilePath[FILE_PATH];
    char szFileUploadTime[MAX_SIZE];
    long long m_szFileSize;
    long long m_pos; //偏移量
    char szFileMD5[MAX_SIZE];
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void showEvent(QShowEvent *event);

    string FileDigest(const string &filr);
private slots:
    void LoginRs(const char* szbuf);
    void GetFileLisRs(const char* szbuf);
    void UploadFileInfoRS(const char* szbuf);
    void SelectFileRs(const char* szbuf);
    void ShareLinkRs(const char* szbuf);
    void GetLinkRs(const char* szbuf);
    void on_action_2_triggered();

    void on_Select_clicked();

    void on_Delete_clicked();

    void on_action_triggered();

    void on_actionSend_File_triggered();

private:
    Ui::MainWindow *ui;
    IKernel* m_pKernel;
    Login* m_pLogin;
    long long Id;
    std::list<uploadFileInfo*> m_lstuploadFileInfo;
    Dialog* m_pDialog;

};
#endif // MAINWINDOW_H
