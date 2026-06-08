#ifndef KERNEL_H
#define KERNEL_H
#pragma once
#include "IKernel.h"
#include "../netWork/tcpnet.h"
#include "../CMySQL/cmysql.h"
#include "packdef.h"
#include <random>

struct fileinfo{
    FILE* pFile;
    long long userId;
    long long fileId;
    long long fileSize;
    long long pos;
};

// kernel：核心业务层，采用饿汉式单例模式
// 持有网络层（INet）和数据库层（CMySql）的指针，统一协调两者的生命周期
class kernel : public IKernel
{
public:
    kernel();
    ~kernel();

public:
    // 获取全局唯一的 kernel 实例（单例访问入口）
    static kernel* GetKernel();

    // 启动服务：依次初始化网络层和 MySQL 连接
    bool open();

    // 关闭服务：依次停止网络层和断开数据库连接
    void close();

    // 处理客户端数据包：根据包类型分发业务逻辑
    void dealData(SOCKET socketWaiter, const char* szbuf);

    void RegisterRq(SOCKET socketWaiter, const char* szbuf);

    void LoginRq(SOCKET socketWaiter, const char *szbuf);

    void GetFileLisRq(SOCKET socketWaiter, const char *szbuf);

    void UploadFileLisRq(SOCKET socketWaiter, const char *szbuf);

    void UploadFileContentRq(SOCKET socketWaiter, const char *szbuf);

    void SelectFileRq(SOCKET socketWaiter, const char *szbuf);

    void DeleteFileRq(SOCKET socketWaiter, const char *szbuf);

    void ShareLinkRq(SOCKET socketWaiter, const char *szbuf);

    void GetLinkRq(SOCKET socketWaiter, const char *szbuf);

    void DownLoadFileRq(SOCKET socketWaiter, const char *szbuf);

private:
    INet*   m_pNet;             // 网络层接口指针（实际指向 TCPNet 实例）
    CMySql* m_pSql;             // 数据库层指针
    static kernel* m_pKernel;   // 单例实例（程序启动时即创建）
    char m_szSystemPath[FILE_PATH]; // 系统路径，用于存储用户文件夹的根目录
    list<fileinfo*> m_lstFileInfo;
};

#endif // KERNEL_H
