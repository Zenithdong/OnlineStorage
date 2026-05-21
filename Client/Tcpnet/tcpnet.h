#ifndef TCPNET_H
#define TCPNET_H
#pragma once
#include "INet.h"
#include "../Kernel/Kernel.h"
#include<iostream>

using namespace std;

class tcpnet : public INet
{
public:
    tcpnet(IKernel* pKernel);
    ~tcpnet();
public:
    bool ConnectServer(const char * szip = "127.0.0.1",short nport = 8899);
    void disConnectServer();
    bool SendData(const char * szbuf ,int nLen);
    void RecvData();

    static DWORD WINAPI ThreadRecv(LPVOID lp);
public:
    SOCKET sockclient;
    bool m_bQuitfFlag;
    HANDLE m_hThread;
    IKernel* m_pKernel;
};



#endif // TCPNET_H
