#include <iostream>
#include "netWork/tcpnet.h"
#include "CMySQL/cmysql.h"

int main()
{
    std::cout << "Server starting..." << std::endl;

    TCPNet* pNet = new TCPNet();
    if (!pNet->InitNetWork(0, 8899)) {
        std::cerr << "Network initialization failed" << std::endl;
        delete pNet;
        return 1;
    }

    std::cout << "Server listening on port 8899..." << std::endl;

    // Keep main thread alive
    while (true) {
        Sleep(1000);
    }

    pNet->UnitNetWork();
    delete pNet;

    return 0;
}
