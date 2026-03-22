#ifndef KERNEL_H
#define KERNEL_H
#include "IKernel.h"
#include "../netWork/tcpnet.h"
#include "../CMySQL/cmysql.h"

class kernel : public IKernel
{
public:
    kernel();
    ~kernel();
public:
    static kernel* GetKernel();
    bool open();
    void close();
    void dealData(SOCKET socketWaiter, const char* szbuf);
private:
    INet* m_pNet;
    CMySql* m_pSql;
    static kernel* m_pKernel;
};

#endif // KERNEL_H
