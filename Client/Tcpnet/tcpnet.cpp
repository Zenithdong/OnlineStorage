#include "tcpnet.h"

tcpnet::tcpnet(IKernel* pKernel) {
    m_bQuitfFlag = true;
    m_hThread = 0;
    m_pKernel = pKernel;
}

tcpnet::~tcpnet()
{
    closesocket(sockclient);
    WSACleanup();
}

bool tcpnet::ConnectServer(const char *szip, short nport)
{
    //1
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
        printf("The Winsock 2.2 dll was found okay\n");

    //2
    sockclient =  socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);   //第一个 ip地址类型    第二个  传输的数据类型  第三个  协议类型
    if(sockclient == INVALID_SOCKET){
        printf("socket err");
        closesocket(sockclient);
        WSACleanup();
        return false;
    }

    //3
    sockaddr_in addrserver;
    addrserver.sin_family = AF_INET;    //地址簇
    addrserver.sin_port = htons(nport);    //端口号
    addrserver.sin_addr.S_un.S_addr=inet_addr(szip);    //指明接收者的ip地址
    if(SOCKET_ERROR == connect(sockclient,(sockaddr*)&addrserver,sizeof(addrserver))){
        printf("connect err");
        closesocket(sockclient);
        WSACleanup();
        return false;
    }
    //为recv 创建线程
    m_hThread = CreateThread(0,0,&ThreadRecv,this,0,0);
    return true;
}

void tcpnet::disConnectServer()
{
    m_bQuitfFlag = false;
    if(m_hThread){
        if(WaitForSingleObject(m_hThread,100) == WAIT_TIMEOUT){
            TerminateThread(m_hThread,-1);
            CloseHandle(m_hThread);
            m_hThread = 0;
        }
    }
}

bool tcpnet::SendData(const char *szbuf, int nLen)
{
    //校验参数
    if(!szbuf || nLen <= 0){
        return false;
    }
    //包大小
    if(send(sockclient, (char*)&nLen,sizeof(int),0)<=0){
        return false;
    }
    //包内容
    if(send(sockclient,szbuf,nLen,0 )<=0){
        return false;
    }
    return true;
}

void tcpnet::RecvData()
{
    int nPackageSize;
    //先接收包大小
    int nRecvNum = recv(sockclient,(char*)&nPackageSize,sizeof(int),0);
    if(nRecvNum<=0){
        closesocket(sockclient);
        return;
    }
    //再接收包内容
    char *pszbuf = new char[nPackageSize];
    int noffset = 0;
    while(nPackageSize){
        nRecvNum = recv(sockclient,pszbuf + noffset,nPackageSize,0);
        noffset += nRecvNum;
        nPackageSize -= nRecvNum;
    }
    //std::cout<<pszbuf<<std::endl;
    m_pKernel->DealData(pszbuf);
    delete[] pszbuf;
    pszbuf = nullptr;
}

DWORD tcpnet::ThreadRecv(LPVOID lp)
{
    tcpnet* pthis = (tcpnet*)lp;
    while(pthis->m_bQuitfFlag){
        pthis->RecvData();
    }
    return 0;
}
