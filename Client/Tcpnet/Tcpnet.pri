# 客户端网络模块：编译 BSD socket TCP 实现（Linux 的 socket 在 libc 中，无需额外链接库）。
HEADERS += \
    $$PWD/INet.h \
    $$PWD/tcpnet.h

SOURCES += \
    $$PWD/tcpnet.cpp
