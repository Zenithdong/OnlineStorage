#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    m_pKernel = new Kernel;
    if(m_pKernel->Connect()) {
        qDebug() << "open success!" ;
    }
    else {
        QMessageBox::critical(this,"result","connect error");
    }
    m_pLogin = new Login(m_pKernel);
    m_pLogin->setWindowTitle("Login Windows");
    m_pLogin->setWindowIcon(QIcon(":/icon.png"));
    m_pLogin->show();
    m_pos = 0;
    connect((Kernel*)m_pKernel, &Kernel::LoginRs, this, &MainWindow::LoginRs,Qt::BlockingQueuedConnection);
    connect((Kernel*)m_pKernel, &Kernel::RegisterRs, m_pLogin, &Login::RegisterRs,Qt::BlockingQueuedConnection);
    connect((Kernel*)m_pKernel, &Kernel::GetFileLisRs, this, &MainWindow::GetFileLisRs,Qt::BlockingQueuedConnection);
    connect((Kernel*)m_pKernel, &Kernel::UploadFileInfoRs, this, &MainWindow::UploadFileInfoRS,Qt::BlockingQueuedConnection);
    connect((Kernel*)m_pKernel, &Kernel::SelectFileRs, this, &MainWindow::SelectFileRs,Qt::BlockingQueuedConnection);
    connect((Kernel*)m_pKernel, &Kernel::ShareLinkRs, this, &MainWindow::ShareLinkRs,Qt::BlockingQueuedConnection);
    connect((Kernel*)m_pKernel, &Kernel::GetLinkRs, this, &MainWindow::GetLinkRs,Qt::BlockingQueuedConnection);
    connect((Kernel*)m_pKernel, &Kernel::DownLoadFileRs, this, &MainWindow::DownLoadFileRs,Qt::BlockingQueuedConnection);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::showEvent(QShowEvent *event)
{
    ui->tableWidget->setColumnCount(3); //给表格设置表头
    QStringList lst;
    lst<<"文件名"<<"文件大小"<<"上传时间";
    ui->tableWidget->setHorizontalHeaderLabels(lst);    //列平铺表格
    //设置表头文字
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget->horizontalHeader()->setStyleSheet("backgground:transparent");
}



string MainWindow::FileDigest(const string &file)
{
    ifstream in(file.c_str(), std::ios::binary);
    if (!in)
        return "";

    MD5 md5;
    std::streamsize length;
    char buffer[1024];
    while (!in.eof()) {
        in.read(buffer, 1024);
        length = in.gcount();
        if (length > 0)
            md5.update(buffer, length);
    }
    in.close();
    return md5.toString();
}

void MainWindow::LoginRs(const char *szbuf)
{
    STRU_LOGIN_RS* sls = (STRU_LOGIN_RS*)szbuf;
    if(sls->szResult == _login_res_failed) {
        QMessageBox::warning(this, "登录结果", "用户名或密码错误！");
    }
    else{
        if(sls->szResult == _login_res_noexist) {
            QMessageBox::information(this, "登陆结果", "用户不存在！");
        }
        else{
            //登录成功
            //登录窗口隐藏
            m_pLogin->hide();
            //mainwindow显示
            this->show();
            //记录UserId，方便后续的获取文件列表操作
            Id = sls->szUserId;
            STRU_GETFILELIST_RQ sgr;
            sgr.szUserId = Id;
            m_pKernel->SendData((char*)&sgr,sizeof(sgr));
        }
    }
}

void MainWindow::GetFileLisRs(const char *szbuf)
{
    STRU_GETFILELIST_RS* psgr = (STRU_GETFILELIST_RS*)szbuf;
    for(int i=0;i<psgr->szFileNum;i++){
        int nRow = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(nRow);

        ui->tableWidget->setItem(i,0,new QTableWidgetItem(QIcon(":/icon1.png"),psgr->arrFileInfo[i].szFileName));
        ui->tableWidget->setItem(i,1,new QTableWidgetItem(QString::number(psgr->arrFileInfo[i].szFileSize)));
        ui->tableWidget->setItem(i,2,new QTableWidgetItem(psgr->arrFileInfo[i].szFileUploadTime));
    }
}

void MainWindow::UploadFileInfoRS(const char *szbuf)
{
    STRU_UPLOADFILEINFO_RS* sur = (STRU_UPLOADFILEINFO_RS*)szbuf;
    //遍历链表获取当前回复所对应的文件信息
    uploadFileInfo* pInfo = nullptr;
    auto ite = m_lstuploadFileInfo.begin();
    while(ite != m_lstuploadFileInfo.end()){
        if(strcmp(sur->szFileMD5,(*ite)->szFileMD5) == 0){
            //找到此文件，并记录
            pInfo = *ite;
            break;
        }
        ite++;
    }

    //根据上传的文件信息回复进行下一步操作
    switch(sur->m_Result){
    case _uploadfileinfo_repeat:
        QMessageBox::warning(this,"提示","你已经上传过文件");
        break;

    case _uploadfileinfo_flashtrans:
    {
        QMessageBox::information(this,"提示","上传成功");
        //将新上传的文件显示在表格中
        //显示pInfo中的文件信息
        break;
    }
    case _uploadfileinfo_continue:
        //断点续传
    case _uploadfileinfo_normal:
        //发送文件发内容
        //打开文件
        STRU_UPLOADFILECONTENT_RQ scr;
        scr.userid = Id;
        scr.fileid = sur->fileid;
        FILE *pFile = fopen(pInfo->szFilePath,"rb");
        //如果是断点续传，进行文件偏移
        if(sur->m_Result == _uploadfileinfo_continue){
            fseek(pFile,sur->m_pos,SEEK_SET);
        }
        //读文件内容并发送
        while(1){
            size_t readNum = fread(scr.m_FileContent, sizeof(char), sizeof(scr.m_FileContent),pFile);
            if(readNum > 0 ) {
                scr.m_fileNum = readNum;
                m_pKernel->SendData((char*)&scr, sizeof(scr));
            }
            else{
                break;
            }
        }
        //关闭文件
        fclose(pFile);
        //将文件信息显示在窗口上

        break;
    }

    if(sur->m_Result != _uploadfileinfo_repeat){
        int nRow = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(nRow);
        QString strFileName = QString::fromStdString(string(pInfo->szFilePath)).section('/',-1);
        ui->tableWidget->setItem(nRow,0,new QTableWidgetItem(QIcon(":/iconl.png"),strFileName));
        ui->tableWidget->setItem(nRow,1,new QTableWidgetItem(QString::number(pInfo->m_szFileSize)));
        ui->tableWidget->setItem(nRow,2,new QTableWidgetItem(pInfo->szFileUploadTime));

        delete pInfo;
        m_lstuploadFileInfo.erase(ite);
    }
}

void MainWindow::SelectFileRs(const char *szbuf)
{
    STRU_SELECTFILE_RS* pssr = (STRU_SELECTFILE_RS*)szbuf;
    for(int i=0;i<pssr->szFileNum;i++){
        int nRow = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(nRow);

        ui->tableWidget->setItem(i,0,new QTableWidgetItem(QIcon(":/icon1.png"),pssr->arrFileInfo[i].szFileName));
        ui->tableWidget->setItem(i,1,new QTableWidgetItem(QString::number(pssr->arrFileInfo[i].szFileSize)));
        ui->tableWidget->setItem(i,2,new QTableWidgetItem(pssr->arrFileInfo[i].szFileUploadTime));
    }
}

// void MainWindow::ShareLinkRs(const char *szbuf)
// {
//     STRU_SHARELINK_RS* psss = (STRU_SHARELINK_RS*)szbuf;
//     QString str = QString("File ").append(psss->szFileName).append(" shared succeessfully!\nExtraction code is: ").append(psss->szCode);
//     QMessageBox::information(this, "Share File",str);
// }

void MainWindow::ShareLinkRs(const char *szbuf)
{
    STRU_SHARELINK_RS* psss = (STRU_SHARELINK_RS*)szbuf;

    QString fileName = QString::fromLocal8Bit(psss->szFileName);
    QString code = QString::fromLocal8Bit(psss->szCode);

    QString str = QString("File %1 shared successfully!\nExtraction code is: %2")
                      .arg(fileName)
                      .arg(code);

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Share File");
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText(str);

    QPushButton* copyBtn = msgBox.addButton("Copy Code", QMessageBox::ActionRole);
    QPushButton* okBtn = msgBox.addButton("OK", QMessageBox::AcceptRole);

    msgBox.exec();
}

void MainWindow::GetLinkRs(const char *szbuf)
{
    STRU_GETLINK_RS *psgs = (STRU_GETLINK_RS*)szbuf;
    if(psgs->szResult == _getlink_failed) {
        QMessageBox::warning(this,"结果", "此文件您已拥有请不要重复提取");
    }else{
        int nRow = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(nRow);
        ui->tableWidget->setItem(nRow,0,new QTableWidgetItem(QIcon(":/iconl.png"),psgs->szFileName));
        ui->tableWidget->setItem(nRow,1,new QTableWidgetItem(QString::number(psgs->szFileSize)));
        ui->tableWidget->setItem(nRow,2,new QTableWidgetItem(psgs->szFileUploadTime));
    }
}

void MainWindow::DownLoadFileRs(const char *szbuf)
{
    STRU_DOWNLOADFILE_RS* psds = (STRU_DOWNLOADFILE_RS*)szbuf;
    //保存的文件路径
    FILE* pFile = fopen(filePath.c_str(),"ab");
    fseek(pFile,m_pos, SEEK_SET);
    int WriteNum = fwrite(psds->m_FileContent,1,psds->m_fileNum,pFile);
    if (WriteNum > 0) {
        m_pos += WriteNum;

    }
    fclose(pFile);
}

void MainWindow::on_action_2_triggered()
{
    qDebug()<<"上传文件被点击了";
    QString filePath = QFileDialog::getOpenFileName(this, tr("打开文件"),
                                                    ".",
                                                    tr("All Files(*.*);;Images (*.png *.xpm *.jpg);;Text files (*.txt);;XML files (*.xml)"));
    qDebug()<<filePath.section('/',-1);
    QString fileName = filePath.section('/',-1);
    if(fileName == "") return;
    //文件大小
    QFile qfile(filePath);
    qfile.open(QIODevice::ReadOnly);
    qint64 fileSize = qfile.size();
    qfile.close();

    //MD5
    string strMD5 = FileDigest(filePath.toStdString());
    //上传时间
    QDateTime time = QDateTime::currentDateTime();
    QString strTime = time.toString("yyyy-MM-dd HH:mm:ss");
    qDebug()<<strTime;

    STRU_UPLOADFILEINFO_RQ sur;
    strcpy_s(sur.szFileMD5,strMD5.c_str());
    strcpy_s(sur.szFileName,fileName.toStdString().c_str());
    strcpy_s(sur.szFileUploadTime,strTime.toStdString().c_str());
    sur.szFilesize = fileSize;
    sur.UserId = Id;

    //收到服务器准许上传文件内容的回复时怎么知道我要传输哪个文件?
    //记录文件信息　文件名、路径、文件ID、文件大小
    uploadFileInfo *pInfo = new uploadFileInfo;
    pInfo->m_szFileSize = fileSize;
    strcpy_s(pInfo->szFileUploadTime,strTime.toStdString().c_str());
    strcpy_s(pInfo->szFilePath,filePath.toStdString().c_str());
    strcpy_s(pInfo->szFileMD5,strMD5.c_str());
    pInfo->m_pos = 0;
    m_lstuploadFileInfo.push_back(pInfo);

    m_pKernel->SendData((char*)&sur,sizeof(sur));
}


void MainWindow::on_Select_clicked()
{
    STRU_SELECTFILE_RQ SSR;
    QString keyWord = ui->lineEdit->text();
    strcpy_s(SSR.m_KeyWord,keyWord.toStdString().c_str());
    SSR.userid = Id;
    m_pKernel->SendData((char*)&SSR,sizeof(SSR));
    ui->tableWidget->setRowCount(0);
}


void MainWindow::on_Delete_clicked()
{
    int nRow = ui->tableWidget->currentRow();
    if(nRow < 0) {
        return;
    }
    QString fileName = ui->tableWidget->item(nRow,0)->text();
    //fileName  Userid 发送给服务器
    STRU_DELETEFILE_RQ sdr;
    sdr.userId = Id;
    strcpy_s(sdr.szFileName,fileName.toStdString().c_str());
    if(m_pKernel->SendData((char*)&sdr,sizeof(sdr))){
        QMessageBox::information(this,"Delete files","Delete successfully!");
        ui->tableWidget->removeRow(nRow);
    }
}
void MainWindow::on_action_triggered()
{
    int nRow = ui->tableWidget->currentRow();
    if(nRow < 0) {
        return;
    }
    QString fileName = ui->tableWidget->item(nRow,0)->text();
    STRU_SHARELINK_RQ ssr;
    ssr.userId = Id;
    strcpy_s(ssr.szFileName,fileName.toStdString().c_str());
    m_pKernel->SendData((char*)&ssr,sizeof(ssr));
}


void MainWindow::on_actionSend_File_triggered()
{
    STRU_GETLINK_RQ sgq;
    //弹出对话框
    m_pDialog = new Dialog(this);
    int result = m_pDialog->exec();
    //判断对话框点击的结果
    if(result == QDialog::Accepted){
        sgq.userId = Id;
        QDateTime time = QDateTime::currentDateTime();
        QString strTime = time.toString("yyyy-MM-dd HH:mm:ss");
        strcpy_s(sgq.szFileUploadTime,strTime.toStdString().c_str());
        strcpy_s(sgq.szCode,m_pDialog->m_code.c_str());
    }
    m_pKernel->SendData((char*)&sgq, sizeof(sgq));
}


void MainWindow::on_action_3_triggered()
{
    STRU_DOWNLOADFILE_RQ sdr;

    //先获取文件信息
    int nRow = ui->tableWidget->currentRow();
    if(nRow == -1)
        return;
    filePath = QFileDialog::getSaveFileName(this, tr("下载文件"),
                                                    "./newDownFile.png",
                                                    tr("Images (*.png *.xpm *.jpg);;All Files(*.*);;"
                                                       "Text files (*.txt);;XML files (*.xml)")).toStdString();

    //文件名字与用户id组合发送至服务器
    string fileName = ui->tableWidget->item(nRow,0)->text().toStdString();
    sdr.userId = Id;
    strcpy_s(sdr.szFileName,fileName.c_str());
    m_pKernel->SendData((char*)&sdr,sizeof(sdr));
}

