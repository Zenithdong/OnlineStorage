#include "mainwindow.h"
#include <QApplication>

/**
 * @brief 客户端程序入口。
 *
 * 所属模块：客户端（进程入口）。
 *
 * 流程：先创建 QApplication 事件循环，再构造主窗口 MainWindow；
 * 主窗口构造期间会建立服务器连接并显示登录页；最后进入 Qt 事件循环，
 * 此后所有 UI 操作与跨线程信号（接收线程 -> 主线程）都在该事件循环中调度。
 *
 * @param argc 命令行参数个数。
 * @param argv 命令行参数数组。
 * @return 事件循环退出码。
 */
int main(int argc, char* argv[]) {
    QApplication a(argc, argv);
    MainWindow w;
    return a.exec();
}
