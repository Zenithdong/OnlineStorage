#include <iostream>
#include <QCoreApplication>
#include "./netWork/tcpnet.h"
#include "./CMySQL/cmysql.h"
#include "./Kernel/kernel.h"
// 程序入口：创建 TCP 网络层并启动服务器
int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    if (!kernel::GetKernel()->open()) {
        std::cerr << "Server failed to start" << std::endl;
        return 1;
    }

    std::cout << "Server started successfully, listening on port 8899..." << std::endl;
    std::cout << "Press Enter to stop the server." << std::endl;

    // 主线程阻塞，select 线程在后台处理客户端连接和数据
    getchar();

    kernel::GetKernel()->close();
    std::cout << "Server stopped." << std::endl;

    return a.exec();
}
