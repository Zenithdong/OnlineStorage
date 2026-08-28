QT += widgets

CONFIG += c++17

# 客户端由登录、注册、提取码对话框和网盘主窗口组成；Qt 的 uic 会根据 FORMS 生成对应界面类。
SOURCES += \
    dialog.cpp \
    login.cpp \
    main.cpp \
    mainwindow.cpp \
    register.cpp

HEADERS += \
    dialog.h \
    login.h \
    mainwindow.h \
    packdef.h \
    register.h

FORMS += \
    dialog.ui \
    login.ui \
    mainwindow.ui \
    register.ui

# 三个子模块分别承担协议分发、Winsock 通信和文件 MD5 计算；.pri 用于集中维护各自的源码与依赖。
include("./Kernel/Kernel.pri")
include("./Tcpnet/Tcpnet.pri")
include("./MD5/MD5.pri")

# qmake 的平台作用域可为类 Unix 系统设置安装目录；Windows 构建不会进入这些分支。
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    image.qrc
