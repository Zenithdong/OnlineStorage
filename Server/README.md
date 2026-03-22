# 服务端文档 (Server)

## 主体类架构

```
┌─────────────────────────────────────────────────────────────┐
│                         main.cpp                            │
│                       (程序入口)                             │
└────────────────────────────┬────────────────────────────────┘
                             │
         ┌───────────────────┼───────────────────┐
         ▼                   ▼                   ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│   kernel     │    │   TCPNet     │    │   CMySQL     │
│  (核心层)     │    │  (网络通信)   │    │   (数据库)    │
└──────┬───────┘    └──────────────┘    └──────────────┘
       │                   ▲
       │                   │
       └───────────────────┘
            继承 INet 接口
```

## 核心类说明

### kernel（核心业务层）

负责统一管理网络层和数据库层，采用单例模式。

| 方法 | 说明 |
|------|------|
| `GetKernel()` | 获取单例实例 |
| `open()` | 初始化网络和数据库连接 |
| `close()` | 关闭所有连接 |
| `dealData()` | 处理客户端请求 |

### TCPNet（网络通信类）

负责 TCP 连接的建立和数据收发，继承自 INet 接口。

| 方法 | 说明 |
|------|------|
| `InitNetWork()` | 初始化网络，绑定端口 8899 |
| `UnitNetWork()` | 关闭网络连接 |
| `sendData()` | 发送数据（长度前缀协议） |
| `recvData()` | 接收数据 |

- 使用 `select()` 模型处理多客户端
- 默认端口：**8899**
- 内部线程：`ThreadSelect` 监控套接字事件

### INet（网络接口基类）

定义网络层统一接口。

### CMySQL（数据库类）

负责与 MySQL 数据库的连接和操作。

| 方法 | 说明 |
|------|------|
| `ConnectMySql()` | 连接数据库 |
| `DisConnect()` | 断开连接 |
| `SelectMysql()` | 执行 SELECT 查询 |
| `UpdateMysql()` | 执行 INSERT/UPDATE/DELETE |

## 协议包定义 (packdef.h)

### 注册协议

| 结构体 | 说明 |
|--------|------|
| `STRU_REGISTER_RQ` | 注册请求（用户名、密码、手机号） |
| `STRU_REGISTER_RS` | 注册响应（结果） |

### 登录协议

| 结构体 | 说明 |
|--------|------|
| `STRU_LOGIN_RQ` | 登录请求（用户名、密码） |
| `STRU_LOGIN_RS` | 登录响应（结果、用户ID） |

### 文件操作协议

| 结构体 | 说明 |
|--------|------|
| `STRU_GETFILELIST_RQ/RS` | 获取文件列表 |
| `STRU_UPLOADFILEINFO_RQ/RS` | 上传文件信息 |
| `STRU_UPLOADFILECONTENT_RQ/RS` | 上传文件内容 |
| `STRU_DELETEFILE_RQ/RS` | 删除文件 |
| `STRU_DOWNFILEINFO_RQ/RS` | 下载文件信息 |
| `STRU_DOWNFILECONTENT_RQ/RS` | 下载文件内容 |

## 目录结构

```
Server/
├── main.cpp              # 程序入口
├── packdef.h             # 协议包定义
├── netWork/
│   ├── INet.h            # 网络接口基类
│   ├── tcpnet.h          # 网络类头文件
│   └── tcpnet.cpp        # 网络类实现
├── CMySQL/
│   ├── cmysql.h          # 数据库类头文件
│   └── cmysql.cpp        # 数据库类实现
├── Kernel/
│   ├── IKernel.h         # 内核接口基类
│   ├── kernel.h          # 核心层头文件
│   └── kernel.cpp        # 核心层实现
└── README.md             # 本文档
```

## 构建与运行

### 环境要求

- Windows 操作系统
- MinGW-w64 编译器
- MySQL Server 8.0
- 依赖库：Winsock2、MySQL Connector/C

### VSCode 构建

按 `Ctrl+Shift+B` 执行构建。

### CMake 构建

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### 运行

1. 确保 MySQL 服务运行中
2. 配置数据库连接参数（在 kernel.cpp 中修改）
3. 运行 `Server.exe`
4. 服务端监听端口 8899

## 网络协议

### 数据包格式

| 字节 | 内容 |
|------|------|
| 0-3 | 包长度 (int) |
| 4-? | 包内容 |

### 通信流程

1. 客户端连接 `ServerIP:8899`
2. 客户端发送：长度(4字节) + 内容
3. 服务器接收并处理
4. 服务器可选择发送响应
