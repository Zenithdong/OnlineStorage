#include "tcpnet.h"
#include <windows.h>

TCPNet::TCPNet()
{
    m_sockListen = 0;
    m_bQuitFlag = true;
    FD_ZERO(&m_fdsets);
}

TCPNet::~TCPNet()
{
    closesocket(m_sockListen);
    WSACleanup();
}

bool TCPNet::InitNetWork(unsigned long dwip, short nport)
{
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    wVersionRequested = MAKEWORD(2, 2);
    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        printf("WSAStartup failed with error: %d\n", err);
        return false;
    }

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        printf("Could not find a usable version of Winsock.dll\n");
        WSACleanup();
        return false;
    }
    else
        printf("server The Winsock 2.2 dll was found okay\n");

    m_sockListen = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sockListen == INVALID_SOCKET) {
        printf("socket err\n");
        closesocket(m_sockListen);
        WSACleanup();
        return false;
    }

    sockaddr_in addrserver;
    addrserver.sin_family = AF_INET;
    addrserver.sin_port = htons(nport);
    addrserver.sin_addr.S_un.S_addr = dwip;

    if (SOCKET_ERROR == bind(m_sockListen, (sockaddr*)&addrserver, sizeof(addrserver))) {
        printf("bind err\n");
        closesocket(m_sockListen);
        WSACleanup();
        return false;
    }

    if (SOCKET_ERROR == listen(m_sockListen, 128)) {
        printf("listen err\n");
        closesocket(m_sockListen);
        WSACleanup();
        return false;
    }

    HANDLE hThread = CreateThread(0, 0, &ThreadSelect, this, 0, 0);
    if (hThread) {
        m_lstHandle.push_back(hThread);
    }

    FD_SET(m_sockListen, &m_fdsets);
    return true;
}

void TCPNet::UnitNetWork()
{
    m_bQuitFlag = false;
    auto ite = m_lstHandle.begin();
    while (ite != m_lstHandle.end()) {
        if (WAIT_TIMEOUT == WaitForSingleObject(*ite, 100)) {
            TerminateThread(*ite, -1);
        }
        CloseHandle(*ite);
        *ite = NULL;
        ite++;
    }
    m_lstHandle.clear();
}

bool TCPNet::sendData(SOCKET sockWaiter, const char* szbuf, int nLen)
{
    if (!szbuf || nLen <= 0 || sockWaiter == INVALID_SOCKET) {
        return false;
    }

    if (send(sockWaiter, (char*)&nLen, sizeof(int), 0) <= 0)
        return false;

    if (send(sockWaiter, szbuf, nLen, 0) <= 0)
        return false;

    return true;
}

void TCPNet::recvData()
{
    for (auto ite = m_lstSocket.begin(); ite != m_lstSocket.end(); ite++) {
        SOCKET sockWaiter = *ite;
        int nPackageSize;

        int nRecvNum = recv(sockWaiter, (char*)&nPackageSize, sizeof(int), 0);

        if (nRecvNum <= 0) {
            if (GetLastError() == 10054) {
                closesocket(*ite);
                ite = m_lstSocket.erase(ite);
            }
            continue;
        }

        char* pszbuf = new char[nPackageSize];
        int noffset = 0;
        while (nPackageSize) {
            nRecvNum = recv(sockWaiter, pszbuf + noffset, nPackageSize, 0);
            noffset += nRecvNum;
            nPackageSize -= nRecvNum;
        }
        std::cout << pszbuf << std::endl;
        delete[] pszbuf;
        pszbuf = nullptr;
    }
    Sleep(10);
}

DWORD TCPNet::ThreadSelect(LPVOID lp)
{
    TCPNet* pthis = (TCPNet*)lp;
    TIMEVAL tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100;
    sockaddr_in addrclient;
    int nLen;
    int Num;
    int nReadNum;
    int nPackSize;
    char* pszbuf;

    while (pthis->m_bQuitFlag) {
        fd_set fdtemp;
        fdtemp = pthis->m_fdsets;

        Num = select(fdtemp.fd_count, &fdtemp, 0, 0, &tv);

        while (Num > 0) {
            if (FD_ISSET(pthis->m_sockListen, &fdtemp)) {
                nLen = sizeof(addrclient);
                SOCKET sockWaiter = accept(pthis->m_sockListen, (sockaddr*)&addrclient, &nLen);
                if (sockWaiter == INVALID_SOCKET) {
                    continue;
                }
                std::cout << "client ip:" << inet_ntoa(addrclient.sin_addr) << " port:" << addrclient.sin_port << std::endl;
                pthis->m_lstSocket.push_back(sockWaiter);
                FD_SET(sockWaiter, &pthis->m_fdsets);
                Num--;
            }
            else {
                for (auto ite = pthis->m_lstSocket.begin(); ite != pthis->m_lstSocket.end(); ite++) {
                    if (FD_ISSET(*ite, &fdtemp)) {
                        SOCKET sockWaiter = *ite;
                        nReadNum = recv(sockWaiter, (char*)&nPackSize, sizeof(int), 0);
                        if (nReadNum <= 0) {
                            Num--;
                            if (GetLastError() == 10054) {
                                closesocket(sockWaiter);
                                ite = pthis->m_lstSocket.erase(ite);
                            }
                            continue;
                        }
                        int noffset = 0;
                        pszbuf = new char[nPackSize];
                        while (nPackSize > 0) {
                            nReadNum = recv(sockWaiter, pszbuf + noffset, nPackSize, 0);
                            noffset += nReadNum;
                            nPackSize -= nReadNum;
                        }
                        std::cout << "client say:" << pszbuf << std::endl;
                        delete[] pszbuf;
                        pszbuf = nullptr;
                        Num--;
                    }
                }
            }
        }
    }
    return 0;
}

DWORD TCPNet::ThreadRecv(LPVOID lp)
{
    TCPNet* pthis = (TCPNet*)lp;

    while (pthis->m_bQuitFlag) {
        pthis->recvData();
    }
    return 0;
}
