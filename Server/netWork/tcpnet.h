#ifndef TCPNET_H
#define TCPNET_H

#include <winsock2.h>
#include <cstdio>
#include <list>
#include <map>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

class TCPNet
{
public:
    TCPNet();
    ~TCPNet();

public:
    bool InitNetWork(unsigned long dwip = 0, short nport = 8899);
    void UnitNetWork();
    bool sendData(SOCKET sockWaiter, const char* szbuf, int nLen);
    void recvData();

public:
    static DWORD WINAPI ThreadSelect(LPVOID lp);
    static DWORD WINAPI ThreadRecv(LPVOID lp);

private:
    SOCKET m_sockListen;
    std::list<HANDLE> m_lstHandle;
    bool m_bQuitFlag;
    std::map<DWORD, SOCKET> m_mapThreadToSocket;
    std::list<SOCKET> m_lstSocket;
    fd_set m_fdsets;
};

#endif // TCPNET_H
