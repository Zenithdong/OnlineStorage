# CLAUDE.md

本文件为 Claude Code (claude.ai/code) 提供在此代码库中工作的指导。

> **语言要求：请始终用中文回答用户的问题和需求。**

## 项目概述

基于 Qt 的在线存储系统，采用客户端/服务器架构。客户端通过自定义二进制 TCP 协议与服务器通信。功能包括：用户注册/登录、文件列表、上传（含 MD5 秒传/断点续传）、删除、搜索、分享链接、提取分享。

## 构建与运行

### 客户端（Qt Widgets GUI）

```bash
cd Client
qmake Client.pro && mingw32-make
```

### 服务器（Qt Core 控制台）

```bash
cd Server
qmake Server.pro && mingw32-make
```

- Qt 版本：6.11.0 MinGW 64 位（见构建目录名）
- 服务器依赖 MySQL C API（`libmysql`），需确保链接器能找到
- 修改 `.pro` 文件后需重新运行 `qmake`
- 客户端运行后默认连接 `127.0.0.1:8899`
- 服务器启动前需先创建 MySQL 数据库，建表 SQL 见 `server.txt`
- 服务器默认监听 8899 端口，文件存储在 `disk_file/` 下

## 架构

### 整体三层架构

```
客户端 UI (Qt Widgets)          服务器 Kernel (单例)
    ↓ Qt 信号槽                      ↓
客户端 Kernel (QObject + IKernel)   服务器网络层 TCPNet (select() 多路复用)
    ↓                                ↓
客户端 Tcpnet (Winsock + 接收线程)   MySQL 数据库 (CMySql)
```

两端通过 `packdef.h` 定义的二进制协议通信——两份 `packdef.h` 内容相同（`Client/packdef.h` 和 `Server/packdef.h`），修改协议时必须同步更新。

### 协议数据包

所有数据包继承自 `STRU_BASE`，第一个字节 `m_nType` 标识包类型（由 `_default_protocol_*` 宏定义）。传输时先发 4 字节包体长度，再发结构体内存。接收方根据 `m_nType` 将缓冲区强制转换为对应结构体。

请求/响应对命名规则：`STRU_<功能>_RQ` 为请求，`STRU_<功能>_RS` 为响应。

### 客户端数据流

1. UI 调用 `IKernel::SendData()` 传入打包好的请求结构体
2. `Kernel` 转发给 `tcpnet::SendData()`
3. `tcpnet` 通过 Winsock 发送（长度前缀协议）
4. 独立接收线程 `ThreadRecv` 读取响应，调用 `IKernel::DealData()`
5. `Kernel::DealData()` 根据 `m_nType` 发射 Qt 信号（如 `LoginRs`）
6. `MainWindow` 中连接的槽函数处理响应并更新 UI

信号槽连接集中在 `MainWindow` 构造函数中。`Kernel` 既是 `QObject`（用于发射信号）也是 `IKernel`（业务接口）。

### 服务器数据流

1. `TCPNet` 使用 `select()` 监控所有客户端套接字
2. 新连接请求 → 接受并加入套接字列表
3. 数据到达 → 读取 4 字节长度 → 读取完整数据包 → 调用 `kernel::dealData()`
4. `dealData()` 根据 `m_nType` 分发到对应处理函数，执行 SQL 操作
5. 通过 `m_pNet->sendData()` 将响应发回客户端

### 关键设计点

- **服务器单例**：`kernel` 使用饿汉式单例（静态初始化时创建），天然线程安全
- **服务器网络模型**：`select()` 单线程多路复用，非线程池模式。两个工作线程——`ThreadSelect`（主逻辑）和 `ThreadRecv`（备用）
- **客户端网络模型**：接收线程用 Win32 `CreateThread` 创建，不阻塞 Qt 事件循环
- **文件存储**：`disk_file/<用户ID>/` 下存储用户文件。上传时通过 `fopen("ab")` 追加写入
- **文件去重**：使用 MD5 校验。相同 MD5 已有记录 → 秒传（引用计数 +1）；同用户同文件且有未完成传输 → 断点续传
- **分享机制**：服务器生成 4 位随机码，存储到 `user_shared` 表。他人通过提取码可获取文件（引用计数 +1）
- **删除**：先删用户-文件映射，若引用计数 > 1 仅减计数，若 = 1 才物理删除文件和记录
- **内存管理**：网络层为每个入站数据包 `new char[]`，处理后 `delete`。服务器上传期间用 `list<fileinfo*>` 跟踪活跃传输

## 数据库

MySQL 数据库 `server`，表结构见 `server.txt`：

| 表 | 用途 |
|---|---|
| `user` | 用户信息（u_id, u_name, u_password, u_tel） |
| `file` | 文件信息（f_id, f_name, f_size, f_path, f_count 引用计数, f_md5） |
| `user_file` | 用户-文件映射（多对多，含上传时间） |
| `user_shared` | 分享链接（uid, fid, code） |
| `ufile` | 视图，JOIN user_file 和 file，简化查询 |

服务器硬编码连接 `127.0.0.1:root:1114`，存储在 `kernel::open()` 中。

## 添加新协议步骤

1. 在两端 `packdef.h` 中定义请求/响应类型宏
2. 添加继承自 `STRU_BASE` 的结构体
3. 服务端：在 `kernel::dealData()` switch 中添加 case，实现处理函数
4. 客户端：在 `Kernel::DealData()` switch 中添加 case，发射信号
5. 在 `Kernel` 头文件中声明新信号
6. 在 `MainWindow` 构造函数中连接信号到槽

## 目录结构

```
Online_storage/
├── Client/                  # Qt Widgets 客户端
│   ├── Client.pro
│   ├── main.cpp
│   ├── mainwindow.{h,cpp,ui}
│   ├── login.{h,cpp,ui}
│   ├── register.{h,cpp,ui}
│   ├── dialog.{h,cpp,ui}
│   ├── packdef.h            # 协议定义（须与 Server 同步）
│   ├── Kernel/
│   ├── Tcpnet/
│   └── MD5/
├── Server/                  # Qt Core 控制台服务器
│   ├── Server.pro
│   ├── main.cpp
│   ├── packdef.h            # 协议定义（须与 Client 同步）
│   ├── Kernel/
│   ├── netWork/
│   └── CMySQL/
├── disk_file/               # 服务器文件存储根目录
└── server.txt               # MySQL 建表 SQL
```

## 注意事项

- 网络层使用 Winsock，仅支持 Windows。移植需替换为 BSD 套接字，`CreateThread` 改为 `std::thread` 或 `QThread`
- 服务器地址/端口硬编码：客户端 `IKernel::Connect()` 默认参数，服务器 `INet::InitNetWork()` 默认参数
- 无自动化测试，需手动运行客户端连接服务器进行功能测试
- MD5 模块仅用于客户端文件哈希
