# 客户端业务模块：IKernel 定义窗口可用的统一入口，Kernel 负责把网络响应转换为 Qt 业务信号。
HEADERS += \
    $$PWD/IKernel.h \
    $$PWD/kernel.h

SOURCES += \
    $$PWD/kernel.cpp
