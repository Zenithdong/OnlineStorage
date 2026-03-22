#include "kernel.h"
#include <iostream>

kernel* kernel::m_pKernel = new kernel;

kernel::kernel()
{
    m_pNet = new TCPNet;
    m_pSql = new CMySql;
}

kernel::~kernel()
{
    delete m_pNet;
    m_pNet = NULL;

    delete m_pSql;
    m_pSql = NULL;
}

kernel* kernel::GetKernel()
{
    // 饿汉式单例模式，线程安全
    return m_pKernel;
}

bool kernel::open()
{
    if (!m_pNet->InitNetWork()) {
        std::cerr << "net error" << std::endl;
        return false;
    }
    if (!m_pSql->ConnectMySql("127.0.0.1", "root", "1114", "server")) {
        std::cerr << "mysql error" << std::endl;
        return false;
    }
    return true;
}

void kernel::close()
{
    m_pNet->UnitNetWork();
    m_pSql->DisConnect();
}

void kernel::dealData(SOCKET socketWaiter, const char* szbuf)
{
    // 待实现业务逻辑
}
