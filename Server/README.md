<div align="center">

# 🧠 Server — 服务端技术文档

**OnlineStorage 服务端内部架构 · 接口 · 协议 完整说明**

</div>

---

## 🏛️ 类层次结构

```
╔══════════════════════════════════════════════════════════════╗
║                      main.cpp  (入口)                         ║
╚══════════════════════════════╤═══════════════════════════════╝
                               │ 调用
╔══════════════════════════════▼═══════════════════════════════╗
║                   kernel  （饿汉式单例）                       ║
║                    implements  IKernel                        ║
║                                                              ║
║   open()  ──────────────────────────────────────────────     ║
║     ├──► 🌐 TCPNet::InitNetWork()   启动监听 :8899           ║
║     └──► 🗃️ CMySQL::ConnectMySql()  连接数据库               ║
║                                                              ║
║   dealData(SOCKET, char*)  ◄─── ThreadSelect 回调（待接入）  ║
║     └──► 按 STRU_BASE::m_nType 路由到各业务函数               ║
╚═══════════════╤══════════════════════╤════════════════════════╝
                │                      │
╔═══════════════▼══════╗  ╔════════════▼═══════════════════════╗
║   🌐 TCPNet           ║  ║   🗃️ CMySQL                        ║
║   implements  INet    ║  ║                                    ║
║                       ║  ║  ConnectMySql()  连接              ║
║  InitNetWork()        ║  ║  DisConnect()    断开              ║
║  UnitNetWork()        ║  ║  SelectMysql()   SELECT 查询       ║
║  sendData()           ║  ║  UpdateMysql()   增删改            ║
║  recvData()           ║  ║                                    ║
║  [ThreadSelect]       ║  ╚════════════════════════════════════╝
╚═══════════════════════╝
```

---

## 🧩 核心类说明

### 🔷 kernel — 核心业务层

> 采用**饿汉式单例**，统筹管理网络与数据库的生命周期，是所有业务逻辑的入口。

| 方法 | 签名 | 说明 |
|------|------|------|
| `GetKernel()` | `static IKernel*` | 获取单例实例 |
| `open()` | `bool` | 初始化网络监听 + 数据库连接 |
| `close()` | `void` | 停止网络线程，断开数据库 |
| `dealData()` | `void(SOCKET, const char*)` | ⚡ 协议路由入口（待实现） |

```cpp
// 正确启动姿势
kernel::GetKernel()->open();
```

---

### 🌐 TCPNet — 网络通信层

> 基于 **Winsock2** 封装，`select()` 模型实现单线程多客户端并发。

| 方法 | 说明 |
|------|------|
| `InitNetWork()` | 初始化 Winsock，绑定并监听 `:8899` |
| `UnitNetWork()` | 优雅关闭所有套接字 |
| `sendData(sock, buf, len)` | 发送数据（长度前缀协议） |
| `recvData(sock, buf, len)` | 接收定长数据 |

**内部线程 `ThreadSelect` 工作流：**
```
┌─────────────────────────────────────────────────────┐
│  while(running)                                     │
│    select(fdSet)  ← 监控所有已连接 socket           │
│      ├─ 新连接   → accept() → 加入 fdSet            │
│      └─ 可读数据 → recv() → [待接入 dealData()]     │
└─────────────────────────────────────────────────────┘
```

---

### 🗃️ CMySQL — 数据库访问层

> 封装 **MySQL Connector/C**，提供简洁的查询与更新接口。

| 方法 | 签名 | 说明 |
|------|------|------|
| `ConnectMySql()` | `bool(host, user, pwd, db)` | 建立连接 |
| `DisConnect()` | `void` | 关闭连接，释放资源 |
| `SelectMysql()` | `bool(sql, nCol, list&)` | SELECT，结果按列平铺入 `list` |
| `UpdateMysql()` | `bool(sql)` | INSERT / UPDATE / DELETE |

```cpp
// 查询示例
std::list<std::string> result;
db.SelectMysql("SELECT name FROM users WHERE id=1", 1, result);
```

---

## 📦 协议包定义 (packdef.h)

### 数据包结构

```
 0        3 4                          N
 ┌─────────┬──────────────────────────┐
 │ Length  │        Payload           │
 │  int32  │  STRU_BASE 结构体内存块  │
 └─────────┴──────────────────────────┘
                   │
              m_nType 字段
                   │
     ┌─────────────┼──────────────┐
     ▼             ▼              ▼
  注册(1/2)    登录(3/4)    文件操作(5~16)
```

### 已定义协议结构体

#### 👤 用户模块

| 结构体 | Type | 字段 |
|--------|:----:|------|
| `STRU_REGISTER_RQ` | 1 | 用户名、密码、手机号 |
| `STRU_REGISTER_RS` | 2 | 结果码 |
| `STRU_LOGIN_RQ` | 3 | 用户名、密码 |
| `STRU_LOGIN_RS` | 4 | 结果码、用户 ID |

#### 📁 文件模块（结构体待实现）

| 结构体 | Type | 功能 |
|--------|:----:|------|
| `STRU_GETFILELIST_RQ/RS` | 5/6 | 获取文件列表 |
| `STRU_UPLOADFILEINFO_RQ/RS` | 7/8 | 上传文件元信息 |
| `STRU_UPLOADFILECONTENT_RQ/RS` | 9/10 | 上传文件内容（分块）|
| `STRU_DELETEFILE_RQ/RS` | 11/12 | 删除文件 |
| `STRU_DOWNFILEINFO_RQ/RS` | 13/14 | 下载文件元信息 |
| `STRU_DOWNFILECONTENT_RQ/RS` | 15/16 | 下载文件内容（分块）|

---

## ⚙️ 配置项

| 配置项 | 位置 | 默认值 |
|--------|------|--------|
| 数据库连接 | `kernel.cpp:39` | `127.0.0.1 / root / 1114 / server` |
| 监听端口 | `tcpnet.cpp` | `8899` |
| MySQL 头文件路径 | `tasks.json / CMakeLists.txt` | `C:/Program Files/MySQL/MySQL Server 8.0/` |

---

## 🏗️ 构建与运行

### 环境要求

```
✔  Windows 10/11
✔  MinGW-w64 (g++ ≥ 8，支持 C++17)
✔  MySQL Server 8.0
✔  libmysql.dll  （已内置于 Server/ 目录）
```

### 构建

```bash
# VSCode 一键构建
Ctrl + Shift + B

# CMake
mkdir build && cd build
cmake ..
cmake --build .
```

### 运行

```bash
# 1. 启动 MySQL 服务
# 2. 修改 kernel.cpp:39 填入正确的数据库凭证
# 3. 启动服务端
./Server.exe
# → 监听 0.0.0.0:8899
```

---

## 🔮 后续开发路线

```
[当前]
  main.cpp 直接实例化 TCPNet（已绕过 kernel）
      ↓ 待修复
[目标]
  main.cpp → kernel::GetKernel()->open()
                  ├── TCPNet（初始化网络）
                  └── CMySQL（连接数据库）

  ThreadSelect 收包后 → kernel::dealData() 路由
      ├── type 1/2 → 注册业务
      ├── type 3/4 → 登录业务
      └── type 5~16 → 文件操作业务（待实现）
```
