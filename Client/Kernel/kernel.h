#ifndef KERNEL_H
#define KERNEL_H
#pragma once
#include <QObject>
#include "IKernel.h"
#include "../Tcpnet/tcpnet.h"
#include "packdef.h"

class Kernel : public QObject, public IKernel
{
    Q_OBJECT
public:
    explicit Kernel(QObject *parent = nullptr);
    ~Kernel();
public:
    bool Connect(const char * szip = "127.0.0.1",short nport = 8899);
    void DisConnect();
    bool SendData(const char* szbuf, int Len);
    void DealData(const char* szbuf);

signals:
    void LoginRs(const char* szbuf);
    void RegisterRs(const char* szbuf);
    void GetFileLisRs(const char* szbuf);
    void UploadFileInfoRs(const char* szbuf);
    void UploadFileContentRs(const char* szbuf);
    void SelectFileRs(const char* szbuf);
    void ShareLinkRs(const char* szbuf);
    void GetLinkRs(const char* szbuf);
    void DownLoadFileRs(const char* szbuf);
private:
    INet* m_pNet;
};

#endif // KERNEL_H
