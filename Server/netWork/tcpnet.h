#ifndef TCPNET_H
#define TCPNET_H
#pragma once
#include "INet.h"
#include <cstdio>
#include <list>
#include <map>
#include <iostream>
#include "../Kernel/kernel.h"

// TCPNet：基于 Winsock2 的 TCP 网络层实现，继承自 INet 接口
// 使用 select() 实现多客户端非阻塞 I/O，无需为每个客户端创建独立线程
class TCPNet : public INet
{
public:
    TCPNet();
    ~TCPNet();

public:
    // 初始化网络：启动 Winsock、创建监听套接字、绑定端口、启动 select 线程
    // dwip：绑定的 IP 地址（0 表示 INADDR_ANY），nport：监听端口（默认 8899）
    bool InitNetWork(unsigned long dwip = 0, short nport = 8899);

    // 反初始化网络：设置退出标志并等待所有工作线程结束
    void UnitNetWork();

    // 向指定客户端套接字发送数据（长度前缀协议：先发4字节长度，再发数据体）
    bool sendData(SOCKET sockWaiter, const char* szbuf, int nLen);

    // 遍历已连接客户端列表，逐一接收数据（阻塞式，已被 ThreadSelect 替代）
    void recvData();

public:
    // select 线程入口：监控所有套接字事件，处理新连接和数据接收
    static DWORD WINAPI ThreadSelect(LPVOID lp);

    // recv 线程入口：循环调用 recvData()（备用，当前主逻辑由 ThreadSelect 承担）
    static DWORD WINAPI ThreadRecv(LPVOID lp);

private:
    SOCKET              m_sockListen;           // 服务器监听套接字
    std::list<HANDLE>   m_lstHandle;            // 工作线程句柄列表
    bool                m_bQuitFlag;            // 线程运行标志（false 时各线程退出）
    std::map<DWORD, SOCKET> m_mapThreadToSocket;// 线程 ID 到套接字的映射（预留）
    std::list<SOCKET>   m_lstSocket;            // 已连接客户端套接字列表
    fd_set              m_fdsets;               // select() 监控的套接字集合
};

#endif // TCPNET_H
