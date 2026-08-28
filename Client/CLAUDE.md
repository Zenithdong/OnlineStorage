# CLAUDE.md

**注意：请使用中文回答所有问题。**

本文件为 Claude Code (claude.ai/code) 提供在此代码库中工作的指导。

## 项目概述

这是一个基于 Qt 的在线存储系统客户端。客户端通过 TCP 使用自定义二进制协议与服务器通信。功能包括用户注册、登录和文件操作（列表、上传、下载、删除）。代码库分为三层：

- **UI 层**：Qt 控件（`MainWindow`、`Login`、`Register`），负责收集用户输入并显示结果。
- **业务层**：`Kernel` 类（实现 `IKernel`），负责路由网络数据并发送 Qt 信号以更新 UI。
- **网络层**：`tcpnet` 类（实现 `INet`），负责处理 Winsock 连接、收发数据包，并运行专用的接收线程。

协议定义集中在 `packdef.h` 中。

## 构建与运行

项目使用 **qmake** 和 **Make**。Qt 版本为 5.15.2（MinGW 64 位）。构建命令：

```bash
qmake Client.pro
make            # Windows 上使用 mingw32-make
```

可执行文件将生成在 `build/Desktop_Qt_5_15_2_MinGW_64_bit-Debug/`（或 `-Release`）目录下。直接运行即可，默认会尝试连接 `127.0.0.1:8899`。

如果需要重新生成 Makefile（例如添加源文件后），请再次运行 `qmake`，然后执行 `make`。

## 架构详情

### 协议数据包

所有网络数据包都继承自 `STRU_BASE`，该结构体包含一个 `char m_nType` 字段。类型由 `packdef.h` 中的宏（`_default_protocol_*`）定义。每个请求/响应对都有对应的结构体（如 `STRU_LOGIN_RQ`、`STRU_LOGIN_RS`）。数据包以原始内存块形式传输；接收方根据 `m_nType` 将缓冲区指针转换为对应的结构体。

### 数据流

1. **UI → Kernel**：UI 类（如 `Login`）调用 `IKernel::SendData()` 并传入打包好的请求结构体。
2. **Kernel → Network**：`Kernel` 将数据转发给 `tcpnet::SendData()`。
3. **Network → Server**：`tcpnet` 先发送数据包大小（4 字节），再发送数据包内容。
4. **Server → Network**：接收线程（`ThreadRecv`）读取大小，分配缓冲区，然后读取数据包。
5. **Network → Kernel**：`tcpnet` 调用 `IKernel::DealData()` 并传入原始缓冲区。
6. **Kernel → UI**：`Kernel::DealData()` 根据 `m_nType` 分发并发射 Qt 信号（如 `LoginRs`）。连接的 UI 槽函数（`MainWindow::LoginRs`）转换缓冲区并更新界面。

### 关键约定

- **信号槽连接**：在 `MainWindow` 构造函数中设置。`Kernel` 发射信号；UI 控件提供公共槽函数。
- **网络线程**：使用 Win32 线程（`CreateThread`）运行 `tcpnet::ThreadRecv`，持续接收数据而不阻塞 UI。
- **内存管理**：网络层为每个传入数据包分配临时缓冲区（`new char[nPackageSize]`），处理完后删除。未使用智能指针。
- **错误处理**：大多数函数返回 `bool`；UI 类在失败时显示 `QMessageBox` 警告。

## 目录结构

```
.
├── Client.pro                    # Qt 项目文件
├── main.cpp                      # 应用程序入口
├── mainwindow.{h,cpp,ui}         # 主窗口（登录前隐藏）
├── login.{h,cpp,ui}              # 登录对话框
├── register.{h,cpp,ui}           # 注册对话框
├── packdef.h                     # 协议定义和结构体
├── image.qrc                     # Qt 资源文件（图标）
├── Kernel/
│   ├── Kernel.pri
│   ├── IKernel.h                 # 业务层抽象接口
│   ├── kernel.h                  `Kernel` 类（QObject + IKernel）
│   └── kernel.cpp
└── Tcpnet/
    ├── Tcpnet.pri
    ├── INet.h                    # 网络层抽象接口
    ├── tcpnet.h                  `tcpnet` 类（Win32 套接字）
    └── tcpnet.cpp
```

## 开发注意事项

- **添加新协议**：
  1. 在 `packdef.h` 中定义请求/响应类型宏。
  2. 添加继承自 `STRU_BASE` 的结构体。
  3. 扩展 `Kernel::DealData()` 以处理新类型并发射相应信号。
  4. 在对应的 UI 类中将信号连接到槽函数。
  5. 更新 UI 以通过 `IKernel::SendData()` 发送请求结构体。

- **网络层**是 Windows 特定的（Winsock、`CreateThread`）。移植到其他平台需要将 Winsock 调用替换为 BSD 套接字，并使用 `std::thread` 或 `QThread`。

- **服务器地址和端口**硬编码在 `IKernel::Connect()` 中（`127.0.0.1:8899`）。如需修改，请更改默认参数或在调用 `Connect()` 时传入明确的值。

- **无自动化测试**。需要手动测试，运行客户端连接兼容的服务器。

## 常见任务

- **修改 `.pro` 文件后重新构建**：再次运行 `qmake`。
- **添加新的 UI 窗口**：在 Qt Designer 中创建 `.ui` 文件，生成 `ui_*.h` 文件（qmake 会自动完成），实现对应的 `.h`/`.cpp` 文件。更新 `Client.pro`（`FORMS`、`HEADERS`、`SOURCES`）。
- **添加新的源代码目录**：创建 `.pri` 文件列出其头文件和源文件，然后在 `Client.pro` 中 `include()` 它。

## 参见

- `packdef.h`：完整的协议规范。
- `Kernel::DealData()`：数据包类型到信号的映射。
- `MainWindow` 构造函数：信号槽连接。
