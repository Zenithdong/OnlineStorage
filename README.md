<div align="center">

```
 ██████╗ ███╗   ██╗██╗     ██╗███╗   ██╗███████╗        ███████╗████████╗ ██████╗ ██████╗  █████╗  ██████╗ ███████╗
██╔═══██╗████╗  ██║██║     ██║████╗  ██║██╔════╝        ██╔════╝╚══██╔══╝██╔═══██╗██╔══██╗██╔══██╗██╔════╝ ██╔════╝
██║   ██║██╔██╗ ██║██║     ██║██╔██╗ ██║█████╗          ███████╗   ██║   ██║   ██║██████╔╝███████║██║  ███╗█████╗
██║   ██║██║╚██╗██║██║     ██║██║╚██╗██║██╔══╝          ╚════██║   ██║   ██║   ██║██╔══██╗██╔══██║██║   ██║██╔══╝
╚██████╔╝██║ ╚████║███████╗██║██║ ╚████║███████╗        ███████║   ██║   ╚██████╔╝██║  ██║██║  ██║╚██████╔╝███████╗
 ╚═════╝ ╚═╝  ╚═══╝╚══════╝╚═╝╚═╝  ╚═══╝╚══════╝        ╚══════╝   ╚═╝    ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝
```

# 🗄️ Online Storage — 私有云存储系统

**基于 Qt6 的 Linux 客户端 + Linux 原生 epoll 服务端的轻量级在线网盘**

[![Language](https://img.shields.io/badge/Language-C%2B%2B17-blue?style=for-the-badge&logo=cplusplus)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/Platform-Linux-FCC624?style=for-the-badge&logo=linux)](https://www.kernel.org/)
[![GUI](https://img.shields.io/badge/GUI-Qt%206.11-41CD52?style=for-the-badge&logo=qt)](https://www.qt.io/)
[![Server](https://img.shields.io/badge/Server-epoll%20Reactor-green?style=for-the-badge)](https://man7.org/linux/man-pages/man7/epoll.7.html)
[![Protocol](https://img.shields.io/badge/Protocol-Custom%20Binary-orange?style=for-the-badge)](Client/packdef.h)
[![Database](https://img.shields.io/badge/Database-MySQL%208.4-4479A1?style=for-the-badge&logo=mysql)](https://www.mysql.com/)

</div>

---

## 📌 项目简介

Online Storage 是一个**客户端/服务器架构的私有云存储系统**，从底层 socket 到数据库全链路手写，不依赖任何第三方网络库：

- **客户端**：Qt 6 Widgets 图形界面，通过 BSD socket 与自定义二进制 TCP 协议通信；
- **服务端**：Linux 原生实现（**去 Qt**），单线程 **epoll Reactor** 事件循环 + MySQL 8 持久化；
- **语言**：C++17（主体 C++11/14，少量高收益 C++17 特性如 `std::filesystem`、`std::string_view`）。

> 项目当前处于**原型/教学阶段**，定位是演示「如何从零实现一个可用的网络存储系统」，
> 代码注释详尽（Doxygen 风格），适合作为 C++ 网络编程与 Qt 开发的学习范本。

---

## ⚡ 功能特性

| 功能 | 说明 |
|------|------|
| 👤 **注册 / 登录** | 用户名 + 密码 + 手机号注册，自动创建个人目录；登录返回用户 ID 作为会话身份 |
| 📂 **文件列表** | 从 `ufile` 视图按用户分页查询（每页 15 条），支持连续分页 |
| ⬆️ **文件上传** | 4KB 分块传输；**MD5 秒传**（相同文件免传）；**断点续传**（中断后从断点续传） |
| ⬇️ **文件下载** | 4KB 分块流式下发，逐块写入本地，无结束标记、按 EOF 收尾 |
| 🗑️ **文件删除** | 引用计数去重存储：多用户共享时仅减计数，最后一个引用才物理删除 |
| 🔍 **文件搜索** | SQL `LIKE` 关键字模糊匹配，限定当前用户 |
| 🔗 **分享链接** | 生成 4 位随机提取码，他人可凭码提取 |
| 📥 **提取分享** | 提取码校验 + 事务内建立文件映射并增加引用计数 |

---

## 🏗️ 架构概览

```
┌──────────────────────────────────────────────────────────┐
│                    👤 客户端（Qt 6 Widgets）               │
│                                                            │
│   Login / Register / Dialog ──┐                            │
│        MainWindow（主窗口）   │  Qt 信号槽（主线程）        │
│             └──────────────┬──┘                            │
│                            ▼                               │
│              🧠 Kernel（QObject + IKernel）                │
│              协议分发 · 发射业务信号（接收线程）            │
│                            │  DealData（回调）              │
│                            ▼                               │
│              📡 tcpnet（BSD socket + 接收线程）            │
│              长度前缀协议 · std::thread · mutex/atomic     │
└────────────────────────────┬──────────────────────────────┘
                             │  TCP · 0.0.0.0:8899
┌────────────────────────────▼──────────────────────────────┐
│                 ⚙️ 服务端（Linux 原生，去 Qt）             │
│                                                            │
│   main（主线程：等待回车/SIGINT/SIGTERM）                  │
│     └── kernel（Meyers 单例）── open() 初始化              │
│            │                                               │
│            ▼                                               │
│   📡 Reactor（单线程 epoll 事件循环）                      │
│      ├─ Epoller（epoll RAII 封装）                         │
│      ├─ Session（每连接解帧状态机 + 收发缓冲）             │
│      └─ 完整帧 → kernel::dealData → 业务 → sendData        │
│            │                                               │
│     ┌──────┴─────────┐                                     │
│     ▼                ▼                                     │
│  🗄️ CMySQL        💾 disk_file/                           │
│  (libmysqlclient)  (文件系统存储，按 user_id 分目录)        │
└──────┬────────────────────────────────────────────────────┘
       │
┌──────▼───────────┐
│  🐬 MySQL 8.4    │
│  user · file     │
│  user_file ·     │
│  user_shared     │
└──────────────────┘
```

**并发模型**：服务端为 **epoll 单线程**（IO 与业务同线程，全程无锁）；客户端为 **主线程 + 接收线程**（跨线程用 `std::mutex` + `std::atomic_bool` + `Qt::QueuedConnection` 信号槽）。

---

## 🗂️ 目录结构

```
Online_storage/
├── Client/                          # Qt Widgets 客户端（Linux）
│   ├── Client.pro                   #   qmake 项目文件
│   ├── main.cpp                     #   QApplication 入口
│   ├── packdef.h                    #   协议定义（须与 Server 逐字一致）
│   ├── image.qrc                    #   图标资源
│   ├── mainwindow.{h,cpp,ui}        #   主窗口：列表/上传/下载/删除/搜索/分享/提取
│   ├── login.{h,cpp,ui}             #   登录窗口
│   ├── register.{h,cpp,ui}          #   注册窗口
│   ├── dialog.{h,cpp,ui}            #   提取码对话框
│   ├── Kernel/                      #   业务层：协议分发 + Qt 信号
│   │   ├── IKernel.h                #     业务接口
│   │   ├── kernel.{h,cpp}           #     QObject 分发中心
│   │   └── Kernel.pri
│   ├── Tcpnet/                      #   网络层：BSD socket
│   │   ├── INet.h                   #     网络接口
│   │   ├── tcpnet.{h,cpp}           #     客户端实现 + 接收线程
│   │   └── Tcpnet.pri
│   └── MD5/                         #   文件哈希
│       ├── md5.{h,cpp}              #     增量 MD5（秒传/去重）
│       └── MD5.pri
│
├── Server/                          # 服务端（纯 C++，去 Qt）
│   ├── Makefile                     #   GNU Make 构建
│   ├── CMakeLists.txt               #   CMake 构建（二选一）
│   ├── main.cpp                     #   入口：信号处理 + 优雅退出
│   ├── packdef.h                    #   协议定义（须与 Client 逐字一致）
│   ├── Kernel/                      #   业务层
│   │   ├── IKernel.h                #     业务接口
│   │   └── kernel.{h,cpp}           #     Meyers 单例 · 请求路由 · 文件 CRUD
│   ├── netWork/                     #   网络层：epoll Reactor
│   │   ├── INet.h                   #     网络接口
│   │   ├── epoller.{h,cpp}          #     epoll RAII 封装
│   │   ├── session.{h,cpp}          #     连接状态机 + 收发缓冲
│   │   └── reactor.{h,cpp}          #     单线程事件循环 + 连接管理
│   └── CMySQL/                      #   数据库层
│       └── cmysql.{h,cpp}           #     MySQL C API 封装
│
├── disk_file/                       # 服务端文件存储根目录（<user_id>/ 下）
├── sql/init.sql                     # 建库建表脚本
├── README.md                        # 本文件
├── CLAUDE.md / AGENTS.md            # 开发约定
├── UNIFIED_PLAN.md                  # Linux 化 + 现代化改造计划（已实施）
└── .clang-format                    # 代码格式（LLVM 基，4 空格，120 列）
```

---

## 📦 环境要求

| 组件 | 客户端 | 服务端 | 最低版本 |
|------|:---:|:---:|------|
| 操作系统 | ✅ Linux | ✅ Linux | 任意 x86-64 发行版 |
| 编译器 | ✅ | ✅ | g++ 12+（C++17；实测 15.2.0） |
| Qt | ✅ | ❌ | Qt 6.4+（实测 6.11.1，仅 core/widgets） |
| OpenGL 运行时 | ✅（GUI） | ❌ | `libgl1-mesa-dev`（提供 `libGL.so`） |
| MySQL 客户端库 | ❌ | ✅ | `libmysqlclient-dev` |
| MySQL 服务端 | ❌ | ✅ | MySQL 8.0+（实测 8.4.10） |

> 服务端**不依赖 Qt**，纯 C++ + epoll + libmysqlclient。

安装依赖（Ubuntu/Debian）：

```bash
sudo apt install build-essential libmysqlclient-dev libgl1-mesa-dev
# 客户端需要 Qt6：可用官方在线安装器装到 ~/Qt，或 sudo apt install qt6-base-dev
```

---

## 🚀 快速开始

### 1. 初始化数据库

```bash
# 启动 MySQL 后，导入建库建表脚本
mysql -uroot -p < sql/init.sql
```

> 脚本会创建数据库 `server` 与 `user`、`file`、`user_file`、`user_shared` 四张表及 `ufile` 视图。
> 启动服务端前需设置数据库密码环境变量（见「配置说明」）：`export MYSQL_PASSWORD=你的密码`。

### 2. 编译服务端（Makefile 或 CMake 二选一）

```bash
cd Server
make                                   # 方式一：GNU Make
# 或
cmake -S . -B build && cmake --build build -j   # 方式二：CMake
```

产物为 `Server/build/Server`（Makefile）或 `Server/build/Server`（CMake）。

### 3. 启动服务端

```bash
cd Server
./build/Server
# 输出：Server started successfully, listening on port 8899...
# 按回车或 Ctrl+C 优雅退出（SIGINT/SIGTERM 均触发优雅收尾）
```

### 4. 编译客户端

```bash
cd Client
/home/zenith/Qt/6.11.1/gcc_64/bin/qmake Client.pro   # 或 PATH 中的 qmake6
make -j$(nproc)
```

产物为 `Client/Client`。

### 5. 启动客户端（需图形环境 X11/Wayland）

```bash
cd Client
./Client        # 默认连接 127.0.0.1:8899，登录后进入文件管理
```

---

## 📡 通信协议说明

### 帧格式

所有报文采用**长度前缀 + 原始结构体内存**，无显式序列化（零拷贝）：

```
┌─────────────────────┬──────────────────────────────────────┐
│   Length (4 字节)    │           Payload (N 字节)           │
│  int32 本机小端       │  STRU_BASE 子类结构体（首字节 m_nType）│
└─────────────────────┴──────────────────────────────────────┘
```

- **字节序**：本机小端（客户端与服务端同为 x86-64，直接 `memcpy` 解释）。
- **路由**：`m_nType`（`char`，结构体首字节）标识消息类型，接收方据此 `reinterpret_cast`。
- **定宽**：跨平台宽度有差异的字段（块长度 `m_fileNum`、分页条数 `szFileNum`）统一为 `int32_t`。
- **约束**：结构体只允许 `char` 数组与定宽整数成员，禁止 `std::string`/`std::vector` 等类类型成员（否则破坏内存布局）。

### 消息类型枚举

共 **11 对请求/响应**（协议号 1~22）：

| 宏 ID | 方向 | 功能 | 关键字段 |
|:---:|:---:|------|------|
| 1 / 2 | RQ/RS | 注册 | `szName[50]` `szpassword[50]` `szTel` → `szResult` |
| 3 / 4 | RQ/RS | 登录 | `szName` `szpassword` → `szResult` `szUserId` |
| 5 / 6 | RQ/RS | 文件列表 | `szUserId` → `arrFileInfo[15]` `szFileNum` |
| 7 / 8 | RQ/RS | 上传文件信息 | `UserId` `szFileName` `szFileMD5` `szFilesize` → `fileid` `m_pos` `m_Result` |
| 9 / 10 | RQ/RS | 上传文件内容 | `userid` `fileid` `m_FileContent[4096]` `m_fileNum` |
| 11 / 12 | RQ/RS | 删除文件 | `userId` `szFileName` |
| 13 / 14 | RQ/RS | 下载文件 | `userId` `szFileName` → `m_FileContent[4096]` `m_fileNum`（分块） |
| 15 / 16 | — | 预留（下载正文） | 未使用 |
| 17 / 18 | RQ/RS | 搜索文件 | `userid` `m_KeyWord` → 分页结果 |
| 19 / 20 | RQ/RS | 分享链接 | `userId` `szFileName` → `szFileName` `szCode` |
| 21 / 22 | RQ/RS | 提取分享 | `userId` `szCode` → `szFileName` `szFileSize` `szResult` |

### 报文示例：登录请求

用户 `alice` 登录（密码 `Abc123`），帧总长 = 4 + `sizeof(STRU_LOGIN_RQ)` = 4 + 101 = 105 字节：

```
帧头（4 字节，int32 小端）:  0x65 0x00 0x00 0x00     # 101 = sizeof(STRU_LOGIN_RQ)
包体（101 字节）:
  0x03                                             # m_nType = 3（登录请求）
  61 6c 69 63 65 00 00 00 ... 00                   # szName[50] = "alice\0" + 补零
  41 62 63 31 32 33 00 00 ... 00                   # szpassword[50] = "Abc123\0" + 补零
```

登录成功响应 `STRU_LOGIN_RS`（帧总长 = 4 + 16）：

```
帧头:  0x10 0x00 0x00 0x00     # 16 = sizeof(STRU_LOGIN_RS)
包体:
  0x04                                             # m_nType = 4（登录响应）
  0x02                                             # szResult = 2（成功）
  <8 字节 long long，小端>                          # szUserId
```

> 完整结构体定义见 [`Client/packdef.h`](Client/packdef.h) 与 [`Server/packdef.h`](Server/packdef.h)（两份逐字一致，改动须同步并 `diff` 校验）。

---

## ⚙️ 配置说明

项目当前采用**硬编码配置（无配置文件）**，均集中在 `Server/Kernel/kernel.cpp`：

| 配置项 | 位置 | 默认值 | 说明 |
|------|------|------|------|
| 监听端口 | `Reactor::InitNetWork` 默认参数 | `8899` | 客户端默认连接同端口 |
| 监听地址 | 同上 `dwip` | `0`（0.0.0.0） | 全本机 IPv4 |
| 存储根目录 | `kernel::kernel()` 的 `m_systemPath` | `/home/zenith/workspace/Online_storage/disk_file/` | 以 `/` 结尾，每位用户建 `<user_id>/` 子目录 |
| 数据库地址 | `kernel::open()` | `127.0.0.1` | |
| 数据库账号 | 同上 | `root` | |
| 数据库密码 | 环境变量 `MYSQL_PASSWORD` | 必填，未设置则启动失败 | 从环境变量读取，避免凭据泄漏到公开仓库 |
| 数据库名 | 同上 | `server` | 由 `sql/init.sql` 创建 |
| 客户端服务端地址 | `Kernel::Connect` 默认参数 | `127.0.0.1:8899` | 可改为远端服务端 IP |

> 若需部署到其它机器，直接修改上述硬编码值并重新编译即可（保持「无配置文件」的教学风格）。

---

## 🛠️ 开发指南

### 代码规范

- **语言标准**：C++17，主体 C++11/14（智能指针、lambda、`auto`、`constexpr`），少量 C++17（`std::filesystem`、`std::string_view`、`std::scoped_lock`）。
- **内存管理**：一律使用 `unique_ptr`/`make_unique` 替代裸 `new`/`delete`；网络层包体用 `unique_ptr<char[]>`；`FILE*`/`MYSQL*` 用带 deleter 的 `unique_ptr` 做 RAII。
- **类型转换**：协议结构体与字节互转用 `reinterpret_cast`，数值用 `static_cast`，禁止 C 风格强转。
- **格式**：`.clang-format`（LLVM 基，4 空格缩进，120 列，指针/引用左对齐）。
- **注释**：Doxygen 风格（`/** @brief @param @return */`），跨线程、生命周期、错误处理必须注释清楚。
- **红线**：两端 `packdef.h` 逐字一致；协议结构体禁止类类型成员。

### 添加新协议

1. **协议定义**：在两端 `packdef.h` 同步新增类型宏 + 结构体（继承 `STRU_BASE`，构造时设 `m_nType`），改完复制到另一端并 `diff` 校验。
2. **服务端**：在 `kernel::dealData()` 的 `switch` 加 `case`（含包长/终止符/范围校验）→ 实现 `*Rq()` 执行 SQL + 文件操作 + `sendData()`。
3. **客户端**：在 `Kernel::DealData()` 的 `switch` 加 `case` → 在 `kernel.h` 声明新信号 → 在 `MainWindow` 构造函数 `connect` 信号到槽 → 实现槽更新 UI。

### 调试技巧

- **服务端日志**：业务关键路径已 `std::cerr/std::cout` 输出（登录成功/密码错误/用户不存在、监听启动、错误码）。
- **协议抓包**：`strace -f -e trace=accept,recvfrom,sendto,epoll_wait ./build/Server` 观察系统调用与收发字节。
- **协议冒烟**：可用 `packdef.h` 直接构造结构体写临时客户端（参考 `tcpnet` 的长度前缀逻辑）验证单个协议。
- **MD5 校验**：标准向量 `MD5("abc") == 900150983cd24fb0d6963f7d28e17f72`。
- **优雅退出验证**：`Ctrl+C`（SIGINT）或 `kill -TERM <pid>`，日志应输出 `Server stopped.` 且进程退出（验证 Reactor 线程 join 不挂死）。

---

## ❓ 常见问题（FAQ）

<details>
<summary><b>服务端启动报 <code>bind failed: Address already in use</code></b></summary>

端口 8899 已被占用（通常是上次启动的服务端未退出）。执行 `pkill -x Server` 后重试；或改用 `ss -tlnp | grep 8899` 找到占用进程。
</details>

<details>
<summary><b>服务端启动报 <code>mysql error</code></b></summary>

数据库连接失败。检查：① MySQL 服务是否运行；② `kernel.cpp` 中密码是否与本机 root 密码一致；③ 是否已执行 `sql/init.sql` 创建 `server` 库；④ `libmysqlclient-dev` 是否已安装。
</details>

<details>
<summary><b>客户端链接报 <code>找不到 -lGL</code></b></summary>

缺少 OpenGL 开发库。执行 `sudo apt install libgl1-mesa-dev` 后重新 `qmake && make`。
</details>

<details>
<summary><b>客户端报 <code>connect error</code></b></summary>

服务端未启动或地址不对。确认服务端已监听 8899；若服务端在远端，修改 `Client/Kernel/kernel.cpp` 中 `Connect` 的默认地址。
</details>

<details>
<summary><b>上传中断后如何续传？</b></summary>

重新上传同一文件：客户端计算相同 MD5，服务端检测到「同用户 + 未完成任务/磁盘残留」会返回 `_uploadfileinfo_continue` 及已写入偏移 `m_pos`，客户端从该偏移续传。服务重启后活动任务表丢失，但磁盘残留文件仍会被识别并续传。
</details>

<details>
<summary><b>删除文件后其它用户还能访问吗？</b></summary>

能。删除采用引用计数：仅解除当前用户映射并 `f_count - 1`；只有 `f_count == 1`（最后一个引用）时才物理删除文件与 `file` 记录。
</details>

---

## 📄 License

本项目采用 **MIT License**。

```
MIT License
Copyright (c) 2025 Online Storage contributors

Permission is hereby granted, free of charge, to any person obtaining a copy ...
```

> 完整 LICENSE 文件可自行添加到仓库根目录。

## 📮 联系方式

- 项目仓库：本地开发环境（`/home/zenith/workspace/Online_storage`），尚未托管到远程。
- 问题反馈：可在仓库提交 Issue，或联系维护者。

---

<div align="center">

**用 C++ 原始力量，构建每一个字节的掌控感。**

*Online Storage — Build your own cloud, byte by byte.*

</div>
