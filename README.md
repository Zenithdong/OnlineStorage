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

**基于 Qt / C++ 的轻量级客户端-服务器在线云盘**

[![Language](https://img.shields.io/badge/Language-C%2B%2B17-blue?style=for-the-badge&logo=cplusplus)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows-0078D4?style=for-the-badge&logo=windows)](https://www.microsoft.com/windows/)
[![GUI](https://img.shields.io/badge/GUI-Qt%206.11-41CD52?style=for-the-badge&logo=qt)](https://www.qt.io/)
[![Network](https://img.shields.io/badge/Network-Winsock2%20TCP-green?style=for-the-badge)](https://docs.microsoft.com/winsock/)
[![Database](https://img.shields.io/badge/Database-MySQL%208.0-orange?style=for-the-badge&logo=mysql)](https://www.mysql.com/)
[![License](https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge)](LICENSE)

</div>

---

## ⚡ 项目亮点

> 从底层 Socket 到数据库全链路手写，追求极致性能与可扩展性。

| 🚀 高性能 | 🔒 安全 | 🧩 模块化 | 📦 轻量 |
|:---:|:---:|:---:|:---:|
| `select()` 多路复用 | 长度前缀协议防粘包 | 三层架构清晰分层 | 无第三方网络库依赖 |
| MD5 秒传 + 断点续传 | 密码 MD5 哈希 | 接口抽象易于替换 | 纯 Winsock2 + MySQL C API |

---

## 📯 系统架构

```
┌───────────────────────────────────────────────────┐
│                   👤 客户端 (Qt GUI)                │
│  ┌─────────┐   ┌──────────┐   ┌───────────────┐  │
│  │  Login  │   │MainWindow│   │   Register    │  │
│  │ 登录窗口 │   │  主窗口   │   │   注册窗口     │  │
│  └────┬────┘   └────┬─────┘   └───────┬───────┘  │
│       │          Qt 信号槽              │          │
│       └────────┬───┴──────────────────┘          │
│                ▼                                  │
│  ┌──────────────────────────────────────────┐    │
│  │         🧠 Kernel (业务层)                 │    │
│  │   QObject + IKernel · 数据路由 · 信号发射  │    │
│  └──────────────────┬───────────────────────┘    │
│                     │                             │
│  ┌──────────────────▼───────────────────────┐    │
│  │         📡 Tcpnet (网络层)                 │    │
│  │   Winsock2 · 独立接收线程 · 长度前缀协议   │    │
│  └──────────────────┬───────────────────────┘    │
└─────────────────────┼────────────────────────────┘
                      │  TCP / Port 8899
┌─────────────────────▼────────────────────────────┐
│                 ⚙️ 服务器 (Qt Core)                │
│  ┌──────────────────────────────────────────┐    │
│  │         📡 TCPNet (网络层)                 │    │
│  │   Winsock2 · select() 多路复用 · 多客户端  │    │
│  └──────────────────┬───────────────────────┘    │
│                     │                             │
│  ┌──────────────────▼───────────────────────┐    │
│  │      🧠 kernel (业务层 · 单例)             │    │
│  │   请求分发 · 注册/登录 · 文件CRUD · 分享   │    │
│  └──────┬───────────────────┬───────────────┘    │
│         │                   │                     │
│  ┌──────▼──────┐   ┌────────▼────────┐          │
│  │ 🗄️ CMySql  │   │ 💾 disk_file/   │          │
│  │ MySQL C API │   │ 文件系统存储     │          │
│  └──────┬──────┘   └─────────────────┘          │
└─────────┼────────────────────────────────────────┘
          │
┌─────────▼──────────┐
│  🐬 MySQL Server   │
│  user · file       │
│  user_file · share │
└────────────────────┘
```

---

## 🗂️ 项目目录结构

```
Online_storage/
├── 📁 Client/                      # Qt Widgets 客户端
│   ├── 📄 Client.pro               #   qmake 项目文件
│   ├── 📄 main.cpp                 #   QApplication 入口
│   ├── 📄 packdef.h                #   协议包定义 (与服务器同步)
│   ├── 📄 image.qrc                #   Qt 资源文件 (图标)
│   ├── 🪟 mainwindow.{h,cpp,ui}    #   主窗口 (文件列表/上传/下载/删除)
│   ├── 🪟 login.{h,cpp,ui}         #   登录对话框
│   ├── 🪟 register.{h,cpp,ui}      #   注册对话框
│   ├── 🪟 dialog.{h,cpp,ui}        #   分享提取对话框
│   ├── 📁 Kernel/                  #   客户端业务层
│   │   ├── IKernel.h               #     业务层抽象接口
│   │   ├── kernel.h / kernel.cpp   #     数据路由 + Qt 信号发射
│   │   └── Kernel.pri
│   ├── 📁 Tcpnet/                  #   客户端网络层
│   │   ├── INet.h                  #     网络层抽象接口
│   │   ├── tcpnet.h / tcpnet.cpp   #     Winsock 客户端实现
│   │   └── Tcpnet.pri
│   └── 📁 MD5/                     #   MD5 哈希模块
│       ├── md5.h / md5.cpp         #     文件 MD5 计算 (秒传/去重)
│       └── MD5.pri
│
├── 📁 Server/                      # Qt Core 控制台服务器
│   ├── 📄 Server.pro               #   qmake 项目文件
│   ├── 📄 main.cpp                 #   QCoreApplication 入口
│   ├── 📄 packdef.h                #   协议包定义 (与客户端同步)
│   ├── 📁 Kernel/                  #   服务器业务层
│   │   ├── IKernel.h               #     业务层抽象接口
│   │   ├── kernel.h / kernel.cpp   #     单例 · 请求路由 · 业务逻辑
│   │   └── Kernel.pri
│   ├── 📁 netWork/                 #   服务器网络层
│   │   ├── INet.h                  #     网络层抽象接口
│   │   ├── tcpnet.h / tcpnet.cpp   #     Winsock select() 服务器
│   │   └── netWork.pri
│   └── 📁 CMySQL/                  #   数据库访问层
│       ├── cmysql.h / cmysql.cpp   #     MySQL C API 封装
│       └── CMySQL.pri
│
├── 📁 disk_file/                   # 服务器文件存储根目录
├── 📄 server.txt                   # MySQL 建表 SQL
├── 📄 README.md                    # 本文件
├── 📄 CLAUDE.md                    # Claude Code 指导文件
├── 📄 Client/CLAUDE.md             # 客户端 Claude Code 指导
└── 📄 .gitignore
```

---

## 🚆 功能进度

### ✅ 已实现

- [x] **用户注册** — 用户名 / 密码 / 手机号，自动创建个人文件夹
- [x] **用户登录** — 密码校验 + 用户 ID 返回
- [x] **TCP 多客户端并发** — `select()` 单线程多路复用，无需线程池
- [x] **文件列表获取** — 分页查询（每页 15 条），按用户过滤
- [x] **文件上传（分块）** — 4KB 分块传输 + MD5 秒传 + 断点续传
- [x] **文件删除** — 引用计数管理，多人共享文件仅减计数
- [x] **文件搜索** — SQL `LIKE` 关键字模糊查询
- [x] **分享链接生成** — 4 位随机码，支持他人提取
- [x] **提取分享文件** — 提取码验证 + 自动建立文件映射

### 🔧 待开发

- [ ] 文件下载功能（协议已定义 `_default_protocol_downfileinfo_*`）
- [ ] 文件重命名
- [ ] 用户头像
- [ ] SSL/TLS 加密传输
- [ ] 跨平台移植（Linux epoll / macOS kqueue）

---

## 📌 通信协议

所有数据包采用 **长度前缀协议**，结构如下：

```
┌────────────────┬──────────────────────────────────┐
│  Length (4 B)  │         Payload (N bytes)        │
│   int32 大端   │   STRU_BASE 子类，通过 m_nType 路由 │
└────────────────┴──────────────────────────────────┘
```

| 协议号 | 方向 | 功能 | 状态 |
|:---:|:---:|------|:---:|
| 1 / 2 | RQ/RS | 用户注册 | ✅ 已定型 |
| 3 / 4 | RQ/RS | 用户登录 | ✅ 已定型 |
| 5 / 6 | RQ/RS | 获取文件列表 | 🔧 可扩展 |
| 7 / 8 | RQ/RS | 上传文件元信息 | 🔧 可扩展 |
| 9 / 10 | RQ/RS | 上传文件内容（分块）| 🔧 可扩展 |
| 11 / 12 | RQ/RS | 删除文件 | 🔧 可扩展 |
| 13 / 14 | RQ/RS | 下载文件元信息 | 📋 已定义 |
| 15 / 16 | RQ/RS | 下载文件内容（分块）| 📋 已定义 |
| 17 / 18 | RQ/RS | 搜索文件 | 🔧 可扩展 |
| 19 / 20 | RQ/RS | 生成分享链接 | ✅ 已定型 |
| 21 / 22 | RQ/RS | 提取分享文件 | ✅ 已定型 |

> 详细协议结构体定义请查阅 `packdef.h`（客户端与服务器各持一份，内容同步）。

---

## ⚡ 快速开始

### 🖥️ 环境要求

```
✅ Windows 10/11
✅ Qt 6.11.0 (MinGW 64-bit)
✅ MySQL Server 8.0
✅ MSYS2 MinGW64 工具链 (g++ 支持 C++17)
```

### 🗄️ 数据库初始化

```sql
-- 创建数据库
CREATE DATABASE server;
USE server;

-- 导入表结构
source server.txt;
```

修改服务器数据库连接配置（`Server/Kernel/kernel.cpp:42`）：

```cpp
m_pSql->ConnectMySql("127.0.0.1", "root", "你的密码", "server");
```

### ⚒️ 构建项目

```bash
# 构建服务器
cd Server
qmake Server.pro && mingw32-make

# 构建客户端
cd Client
qmake Client.pro && mingw32-make
```

### 🖼️ 启动服务

```bash
# 1. 确保 MySQL 服务运行中

# 2. 启动服务器（监听 0.0.0.0:8899）
./Server/Server.exe

# 3. 启动客户端（连接 127.0.0.1:8899）
./Client/Client.exe
```

---

## 📚 详细文档

| 文档 | 说明 |
|------|------|
| **[Server/DOCS.md](Server/DOCS.md)** | 服务器端各模块详细代码分析 |
| **[Client/DOCS.md](Client/DOCS.md)** | 客户端各模块详细代码分析 |
| **[CLAUDE.md](CLAUDE.md)** | Claude Code AI 助手指南 |
| **[server.txt](server.txt)** | 数据库表结构 DDL |

---

## 🔑 核心设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 服务器 I/O 模型 | `select()` | 客户端量少时比 IOCP 简单，代码可读性高 |
| 客户端接收模型 | 独立 `CreateThread` | 避免阻塞 Qt 事件循环 |
| 服务器实例管理 | 饿汉式单例 | 静态初始化天然线程安全 |
| 数据包分发 | `switch(m_nType)` | 直白高效，无虚函数开销 |
| 文件去重 | MD5 哈希 | 简单可靠，秒传/断点续传均依赖 |
| 内存管理 | 原生 `new`/`delete` | 避免 Qt 父对象机制与网络层交叉问题 |
| 协议格式 | 二进制结构体 + 长度前缀 | 零序列化开销，直接内存拷贝 |

---

<div align="center">

**用 C++ 原始力量，构建每一个字节的掌控感。**

*Online Storage — Build your own cloud, byte by byte.*

</div>
