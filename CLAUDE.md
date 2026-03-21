# CLAUDE.md

此文件为 Claude Code (claude.ai/code) 提供在此代码仓库中工作的指导。

## 限制
- **所有回答必须使用简体中文**
- 每次回复都要用中文

## 构建命令

**VSCode (Ctrl+Shift+B)** - 使用 tasks.json 通过 g++ 编译
**CMake** - 使用 CMake 工具扩展或手动运行：
```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## 架构

这是一个基于 Windows 的**网盘服务器**应用，采用客户端-服务器 TCP 架构。

### 核心组件

- **TCPNet** (`Server/netWork/`) - 使用 Winsock2 的 TCP 网络层
  - 使用 `select()` 处理多客户端（非每客户端一线程）
  - `ThreadSelect` - 通过 fd_set 监控套接字事件的主线程
  - `ThreadRecv` - 可选的轮询接收线程
  - 默认端口：8899

- **CMySql** (`Server/CMySQL/`) - MySQL 数据库连接
  - `ConnectMySql()` - 建立连接
  - `SelectMysql()` - 查询，结果通过 std::list<std::string> 返回
  - `UpdateMysql()` - 执行 INSERT/UPDATE/DELETE

- **main.cpp** - 入口点，初始化 TCPNet 并进入阻塞循环

### 协议
TCP 协议使用长度前缀数据包：
1. 前4字节：数据包长度 (int)
2. 后续字节：数据包内容

### 依赖
- Winsock2 (`ws2_32.lib`) - Windows 网络
- MySQL Connector/C - 数据库访问（硬编码路径：`C:/Program Files/MySQL/MySQL Server 8.0/`）
