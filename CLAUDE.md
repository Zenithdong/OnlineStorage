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

- **kernel** (`Server/Kernel/`) - 核心业务层（单例模式）
  - 统一管理网络层和数据库层
  - `open()` - 初始化网络和数据库
  - `close()` - 关闭连接
  - `dealData()` - 处理客户端请求

- **INet / TCPNet** (`Server/netWork/`) - 网络层接口与实现
  - 使用 Winsock2 的 TCP 网络层
  - 使用 `select()` 处理多客户端（非每客户端一线程）
  - `ThreadSelect` - 通过 fd_set 监控套接字事件的主线程
  - 默认端口：8899

- **CMySql** (`Server/CMySQL/`) - MySQL 数据库连接
  - `ConnectMySql()` - 建立连接
  - `SelectMysql()` - 查询，结果通过 std::list<std::string> 返回
  - `UpdateMysql()` - 执行 INSERT/UPDATE/DELETE

- **packdef.h** (`Server/`) - 协议包定义
  - 注册、登录、文件操作等协议结构体

### 层次结构

```
main.cpp
    └── kernel (单例模式)
            ├── TCPNet (继承INet) → 网络层
            └── CMySql → 数据库层
```

### 协议
TCP 协议使用长度前缀数据包：
1. 前4字节：数据包长度 (int)
2. 后续字节：数据包内容

### 依赖
- Winsock2 (`ws2_32.lib`) - Windows 网络
- MySQL Connector/C - 数据库访问（硬编码路径：`C:/Program Files/MySQL/MySQL Server 8.0/`）
