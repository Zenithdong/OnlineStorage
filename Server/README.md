# 网盘服务器 (OnlineStorage Server)

## 项目简介

这是一个基于 Windows 的网盘服务器应用程序，采用 TCP 协议与客户端通信，数据存储使用 MySQL 数据库。

## 功能特性

- **TCP 网络通信**：支持多客户端连接，使用 `select()` 模型实现并发处理
- **MySQL 数据库**：用户管理、文件元数据存储
- **自定义协议**：使用长度前缀数据包格式，确保数据完整性
- **端口 8899**：默认监听端口

## 目录结构

```
OnlineStorage/
├── Server/
│   ├── main.cpp           # 程序入口
│   ├── netWork/           # 网络通信模块
│   │   ├── tcpnet.h
│   │   └── tcpnet.cpp
│   └── CMySQL/            # 数据库模块
│       ├── cmysql.h
│       └── cmysql.cpp
├── CMakeLists.txt         # CMake 构建配置
└── .vscode/               # VSCode 配置
    ├── tasks.json         # 构建任务
    ├── launch.json        # 调试配置
    └── c_cpp_properties.json
```

## 环境要求

- **操作系统**：Windows
- **编译器**：MinGW-w64 (g++)
- **数据库**：MySQL Server 8.0
- **依赖库**：
  - Winsock2 (`ws2_32.lib`)
  - MySQL Connector/C

## 构建方式

### 方式一：VSCode 构建

1. 用 VSCode 打开 `D:\Project_C++\OnlineStorage` 文件夹
2. 按 `Ctrl+Shift+B` 执行默认构建任务
3. 可执行文件输出到 `Server\Server.exe`

### 方式二：CMake 构建

```bash
cd D:\Project_C++\OnlineStorage
mkdir build
cd build
cmake ..
cmake --build .
```

## 运行方式

1. 确保 MySQL 服务正在运行
2. 确保数据库已创建并配置好相应表结构
3. 运行 `Server\Server.exe`
4. 服务器将在端口 8899 监听

## 数据库配置

代码中 MySQL 路径硬编码为：`C:/Program Files/MySQL/MySQL Server 8.0/`

如需修改数据库连接，请编辑 `Server/CMySQL/cmysql.cpp` 中的连接参数。

## 网络协议

### 数据包格式

| 字节数 | 内容 |
|--------|------|
| 4 字节 | 包长度 (int，大端序) |
| N 字节 | 包内容 |

### 通信流程

1. 客户端连接服务器 (端口 8899)
2. 客户端发送数据包（先长度后内容）
3. 服务器接收并处理
4. 服务器可选择发送响应

## 注意事项

- 本项目原为 Qt 项目，已转换为标准 C++ 以支持 VSCode 编译
- MySQL 库路径在配置文件中硬编码，如安装路径不同请自行修改
- 服务器使用 `select()` 模型，单线程处理多客户端
