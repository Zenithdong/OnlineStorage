# CLAUDE.md

本文件为 Claude Code (claude.ai/code) 提供在此代码库中工作的指导。

> **语言要求：请始终用中文回答用户的问题和需求。**

## 项目概述

基于 Qt 的在线存储系统，采用客户端/服务器架构，通过自定义二进制 TCP 协议通信。功能包括：用户注册/登录、文件列表、上传（MD5 秒传/断点续传）、下载、删除、搜索、分享链接、提取分享。

## 构建与运行

```bash
# 客户端（Qt Widgets GUI）
cd Client && qmake Client.pro && mingw32-make

# 服务器（Qt Core 控制台）
cd Server && qmake Server.pro && mingw32-make
```

- Qt **6.11.0** MinGW 64 位，C++17
- 服务器依赖 MySQL C API（`libmysql`），需链接器能找到
- 修改 `.pro` 文件后需重新运行 `qmake`
- 服务器启动前先执行 `sql/init.sql` 初始化 MySQL 数据库 `server`
- 服务器监听 `0.0.0.0:8899`，文件存储在 `disk_file/` 下
- 客户端默认连接 `127.0.0.1:8899`

## 架构

### 三层架构概览

```
客户端 UI (Qt Widgets)          服务器 kernel (饿汉单例)
    ↓ Qt 信号槽                      ↓
客户端 Kernel (QObject+IKernel)  服务器 TCPNet (select()多路复用)
    ↓                                ↓
客户端 tcpnet (Winsock+接收线程)   MySQL 数据库 (CMySql)
```

两端通过 `packdef.h` 定义的二进制协议通信——`Client/packdef.h` 和 `Server/packdef.h` 内容必须同步。

### 协议数据包

所有包继承 `STRU_BASE`，首字节 `m_nType` 标识类型，宏定义见 `_default_protocol_*`。传输时先发 4 字节包体长度，再发结构体内存。接收方根据 `m_nType` 强制转换缓冲区。

已实现协议（共 11 对请求/响应）：

| 宏 ID | 功能 | 状态 |
|:---:|------|:---:|
| 1/2 | 注册 | 完成 |
| 3/4 | 登录 | 完成 |
| 5/6 | 获取文件列表（每页 15 条） | 完成 |
| 7/8 | 上传文件信息 | 完成 |
| 9/10 | 上传文件内容（4KB 分块） | 完成 |
| 11/12 | 删除文件 | 完成 |
| 13/14 | 下载文件信息 | 完成 |
| 15/16 | 下载文件内容（4KB 分块） | 完成 |
| 17/18 | 搜索文件（SQL LIKE 模糊查询） | 完成 |
| 19/20 | 生成分享链接（4 位随机码） | 完成 |
| 21/22 | 提取分享文件 | 完成 |

### 客户端数据流

1. UI 调用 `IKernel::SendData()` 发送请求结构体
2. `Kernel` 转发给 `tcpnet::SendData()`
3. `tcpnet` 通过 Winsock 发送（长度前缀协议）
4. 独立接收线程 `ThreadRecv`（用 `CreateThread`）读取响应，调用 `IKernel::DealData()`
5. `Kernel::DealData()` 根据 `m_nType` 发射 Qt 信号（如 `LoginRs`）
6. `MainWindow` 中连接的槽函数处理响应并更新 UI

信号槽连接集中在 `MainWindow` 构造函数中，使用 `Qt::BlockingQueuedConnection`。`Kernel` 既是 `QObject`（发射信号）也是 `IKernel`（业务接口）。

### 服务器数据流

1. `TCPNet` 使用 `select()` 监控所有客户端套接字（`m_lstSocket`），两个工作线程：`ThreadSelect` 处理主逻辑，`ThreadRecv` 备用
2. 新连接 → 接受并加入套接字列表
3. 数据到达 → 读取 4 字节长度 → 读取完整包 → 调用 `kernel::dealData()`
4. `dealData()` 根据 `m_nType` switch 分发到对应 `*Rq()` 处理函数
5. 处理函数执行 SQL 操作后，通过 `m_pNet->sendData()` 发回响应

### 关键设计点

- **服务器单例**：`kernel::m_pKernel` 饿汉式（静态初始化时 `new kernel`），天然线程安全
- **文件去重**：MD5 校验。相同 MD5 已有记录 → 秒传（`f_count` 引用计数 +1）；同用户同文件且有未完成传输 → 断点续传（服务器用 `list<fileinfo*>` 跟踪活跃传输）
- **分享机制**：服务器生成 4 位随机码存入 `user_shared` 表，提取时引用计数 +1
- **删除**：先删 `user_file` 映射，若 `f_count > 1` 仅减计数，若 `= 1` 才物理删除文件和 `file` 记录
- **内存管理**：网络层为每个入站包 `new char[]`，处理后 `delete`。无智能指针

## 数据库

MySQL 数据库 `server`，建表脚本见 `sql/init.sql`：

| 表 | 用途 |
|---|---|
| `user` | 用户信息（u_id, u_name, u_password, u_tel） |
| `file` | 文件实体（f_id, f_name, f_size, f_path, f_count 引用计数, f_md5） |
| `user_file` | 多对多映射（u_id, f_id, time） |
| `user_shared` | 分享链接（uid, fid, code） |
| `ufile` | 视图（JOIN user_file 和 file） |

数据库连接硬编码在 `Server/Kernel/kernel.cpp:open()` 中（`127.0.0.1:root:1114`）。

## 添加新协议步骤

1. 在两端 `packdef.h` 同步更新：定义请求/响应类型宏 + 结构体
2. 服务端：在 `kernel::dealData()` switch 加 case → 实现 `*Rq()` 方法执行 SQL 和发送响应
3. 客户端：在 `Kernel::DealData()` switch 加 case → 发射新信号
4. 在 `Kernel` 头文件中声明新信号
5. 在 `MainWindow` 构造函数连接信号到槽，实现槽函数处理 UI 更新

## 目录结构

```
Online_storage/
├── Client/                    # Qt Widgets 客户端
│   ├── Client.pro             # qmake 项目，include Kernel/Tcpnet/MD5 的 .pri
│   ├── main.cpp               # QApplication 入口
│   ├── mainwindow.{h,cpp,ui}  # 主窗口：文件列表表格、上传/下载/删除/搜索/分享
│   ├── login.{h,cpp,ui}       # 登录对话框
│   ├── register.{h,cpp,ui}    # 注册对话框
│   ├── dialog.{h,cpp,ui}      # 分享提取码对话框
│   ├── packdef.h              # 协议定义（须与 Server 同步）
│   ├── image.qrc              # 图标资源
│   ├── Kernel/                # 业务层：数据路由 + Qt 信号发射
│   ├── Tcpnet/                # 网络层：Winsock + 独立接收线程
│   ├── MD5/                   # 文件 MD5 哈希计算（秒传/去重）
│   ├── DOCS.md                # 客户端模块详细文档
│   └── CLAUDE.md              # 客户端 CLAUDE.md
├── Server/                    # Qt Core 控制台服务器
│   ├── Server.pro             # qmake 项目，include 三个 .pri
│   ├── main.cpp               # QCoreApplication 入口
│   ├── packdef.h              # 协议定义（须与 Client 同步）
│   ├── Kernel/                # 核心业务（单例）：注册/登录/文件 CRUD/分享
│   ├── netWork/               # 网络层：Winsock2 select() 多路复用
│   ├── CMySQL/                # MySQL C API 封装（SelectMysql/UpdateMysql）
│   └── DOCS.md                # 服务器模块详细文档
├── disk_file/                 # 文件存储根目录（/<user_id>/ 下）
└── sql/init.sql               # 数据库建库建表脚本
```

## 详细文档

- `Client/DOCS.md` — 客户端各模块详细代码分析
- `Server/DOCS.md` — 服务器端各模块详细代码分析
- `README.md` — 项目功能进度、协议速查表、快速开始

## 注意事项

- 网络层使用 Winsock，仅支持 Windows。移植需替换为 BSD 套接字，`CreateThread` 改为 `std::thread` 或 `QThread`
- 服务器地址/端口、数据库连接参数均硬编码，无配置文件
- 客户端上传流程：选择文件 → MD5 计算 → 发送文件信息 → 根据服务器回复（秒传/断点续传/正常上传）决定下一操作
- 客户端下载流程：`on_action_3_triggered()` 发送 `STRU_DOWNLOADFILE_RQ` → 服务器分块发送文件内容 → `DownLoadFileRs` 逐块写入本地文件
- UI 使用 `uploadFileInfo` 结构体链表跟踪待上传文件（文件名/路径/MD5/偏移量）
- 服务器使用 `fileinfo` 结构体链表跟踪活跃上传（文件指针/用户ID/文件ID/位置）
- 无自动化测试，需手动运行客户端连接服务器进行功能测试
