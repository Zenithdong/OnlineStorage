#include "tcpnet.h"
#include <windows.h>

TCPNet::TCPNet()
{
    m_sockListen = 0;
    m_bQuitFlag = true;   // true 表示线程应持续运行
    FD_ZERO(&m_fdsets);   // 清空 select 套接字集合
}

TCPNet::~TCPNet()
{
    closesocket(m_sockListen);
    WSACleanup();  // 释放 Winsock 资源
}

// 初始化网络层：加载 Winsock → 创建套接字 → 绑定 → 监听 → 启动 select 线程
bool TCPNet::InitNetWork(unsigned long dwip, short nport)
{
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    // 请求 Winsock 2.2 版本
    wVersionRequested = MAKEWORD(2, 2);
    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        printf("WSAStartup failed with error: %d\n", err);
        return false;
    }

    // 校验实际获得的 Winsock 版本是否为 2.2
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        printf("Could not find a usable version of Winsock.dll\n");
        WSACleanup();
        return false;
    }
    else
        printf("server The Winsock 2.2 dll was found okay\n");

    // 创建 TCP 流式套接字
    m_sockListen = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sockListen == INVALID_SOCKET) {
        printf("socket err\n");
        closesocket(m_sockListen);
        WSACleanup();
        return false;
    }

    // 允许端口复用，避免服务端重启时 bind 失败
    int opt = 1;
    setsockopt(m_sockListen, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    // 配置服务端地址：协议族、端口（网络字节序）、绑定 IP
    sockaddr_in addrserver;
    addrserver.sin_family = AF_INET;
    addrserver.sin_port = htons(nport);
    addrserver.sin_addr.S_un.S_addr = dwip;

    // 绑定监听套接字到指定地址
    if (SOCKET_ERROR == bind(m_sockListen, (sockaddr*)&addrserver, sizeof(addrserver))) {
        printf("bind err\n");
        closesocket(m_sockListen);
        WSACleanup();
        return false;
    }

    // 开始监听，最大挂起连接队列长度为 128
    if (SOCKET_ERROR == listen(m_sockListen, 128)) {
        printf("listen err\n");
        closesocket(m_sockListen);
        WSACleanup();
        return false;
    }

    // 启动 select 线程，负责监控所有套接字事件（接受连接 + 接收数据）
    HANDLE hThread = CreateThread(0, 0, &ThreadSelect, this, 0, 0);
    if (hThread) {
        m_lstHandle.push_back(hThread);
    }

    // 将监听套接字加入 select 集合，以便检测新连接事件
    FD_SET(m_sockListen, &m_fdsets);
    return true;
}

// 停止网络层：通知线程退出并等待其结束，超时则强制终止
void TCPNet::UnitNetWork()
{
    m_bQuitFlag = false;  // 通知所有工作线程退出循环
    auto ite = m_lstHandle.begin();
    while (ite != m_lstHandle.end()) {
        // 等待线程结束，超时 100ms 则强制终止
        if (WAIT_TIMEOUT == WaitForSingleObject(*ite, 100)) {
            TerminateThread(*ite, -1);
        }
        CloseHandle(*ite);
        *ite = NULL;
        ite++;
    }
    m_lstHandle.clear();
}

// 发送数据（长度前缀协议）：先发送 4 字节包长，再发送数据体
bool TCPNet::sendData(SOCKET sockWaiter, const char* szbuf, int nLen)
{
    if (!szbuf || nLen <= 0 || sockWaiter == INVALID_SOCKET) {
        return false;
    }

    // 发送包头：数据长度（4 字节）
    if (send(sockWaiter, (char*)&nLen, sizeof(int), 0) <= 0)
        return false;

    // 发送包体：实际数据
    if (send(sockWaiter, szbuf, nLen, 0) <= 0)
        return false;

    return true;
}

// 遍历客户端列表接收数据（阻塞式，主逻辑已由 ThreadSelect 承担）
void TCPNet::recvData()
{
    for (auto ite = m_lstSocket.begin(); ite != m_lstSocket.end(); ite++) {
        SOCKET sockWaiter = *ite;
        int nPackageSize;

        // 接收包头：读取数据包长度（4 字节）
        int nRecvNum = recv(sockWaiter, (char*)&nPackageSize, sizeof(int), 0);

        if (nRecvNum <= 0) {
            // 错误码 10054（WSAECONNRESET）：对端强制关闭连接
            if (GetLastError() == 10054) {
                closesocket(*ite);
                ite = m_lstSocket.erase(ite);
            }
            continue;
        }

        // 接收包体：循环读取直至收齐完整数据包
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
    Sleep(10);  // 避免空转占满 CPU
}

// select 线程：使用 select() 多路复用监控所有套接字，处理新连接和数据接收
DWORD TCPNet::ThreadSelect(LPVOID lp)
{
    TCPNet* pthis = (TCPNet*)lp;
    TIMEVAL tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100;  // select 超时 100 微秒，防止阻塞退出检测
    sockaddr_in addrclient;
    int nLen;
    int Num;       // select 返回的就绪套接字数量
    int nReadNum;  // 单次 recv 返回的字节数
    int nPackSize; // 当前数据包长度
    char* pszbuf;

    while (pthis->m_bQuitFlag) {
        // 每次循环复制 fd_set，因为 select() 会修改传入的集合
        fd_set fdtemp;
        fdtemp = pthis->m_fdsets;

        Num = select(fdtemp.fd_count, &fdtemp, 0, 0, &tv);

        while (Num > 0) {
            // 优先检查监听套接字是否有新连接请求
            if (FD_ISSET(pthis->m_sockListen, &fdtemp)) {
                nLen = sizeof(addrclient);
                SOCKET sockWaiter = accept(pthis->m_sockListen, (sockaddr*)&addrclient, &nLen);
                if (sockWaiter == INVALID_SOCKET) {
                    continue;
                }
                // 打印新连接的客户端 IP 和端口
                std::cout << "client ip:" << inet_ntoa(addrclient.sin_addr) << " port:" << addrclient.sin_port << std::endl;
                pthis->m_lstSocket.push_back(sockWaiter);
                FD_SET(sockWaiter, &pthis->m_fdsets);  // 将新客户端加入监控集合
                FD_CLR(pthis->m_sockListen, &fdtemp);  // 从临时集合中移除监听套接字，避免重复处理
                Num--;
            }
            else {
                // 遍历客户端列表，找出就绪（有数据可读）的套接字
                for (auto ite = pthis->m_lstSocket.begin(); ite != pthis->m_lstSocket.end(); ite++) {
                    if (FD_ISSET(*ite, &fdtemp)) {
                        SOCKET sockWaiter = *ite;

                        // 接收包头：读取数据包长度（4 字节）
                        nReadNum = recv(sockWaiter, (char*)&nPackSize, sizeof(int), 0);
                        if (nReadNum <= 0) {
                            Num--;
                            // 错误码 10054：对端强制断开，从列表中移除并关闭套接字
                            if (GetLastError() == 10054) {
                                closesocket(sockWaiter);               // 关闭套接字
                                FD_CLR(sockWaiter, &pthis->m_fdsets);  // 从监控集合中移除
                                ite = pthis->m_lstSocket.erase(ite);   // 从客户端列表中移除
                            }
                            continue;
                        }

                        // 接收包体：循环读取直至收齐完整数据包
                        int noffset = 0;
                        pszbuf = new char[nPackSize];
                        while (nPackSize > 0) {
                            nReadNum = recv(sockWaiter, pszbuf + noffset, nPackSize, 0);
                            noffset += nReadNum;
                            nPackSize -= nReadNum;
                        }
                        //std::cout << "client say:" << pszbuf << std::endl;
                        kernel::GetKernel()->dealData(*ite, pszbuf);  // 将接收到的数据交给 kernel 处理
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

// recv 线程：循环调用 recvData()（备用线程，主逻辑由 ThreadSelect 承担）
DWORD TCPNet::ThreadRecv(LPVOID lp)
{
    TCPNet* pthis = (TCPNet*)lp;

    while (pthis->m_bQuitFlag) {
        pthis->recvData();
    }
    return 0;
}
