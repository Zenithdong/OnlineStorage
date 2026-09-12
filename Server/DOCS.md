# 📡 服务器端模块文档

> 服务器端为 **Linux 控制台程序**，已彻底脱离 Qt，基于 **单线程 epoll Reactor + MySQL C API** 构建。
> 监听 `0.0.0.0:8899`，支持多客户端并发连接，业务（MySQL + 磁盘 IO）直接内联在 Reactor 事件循环中执行。
> 编译标准 **C++17**，构建系统 **CMake**。

---

## 目录

- [构建与运行](#0-构建与运行)
- [程序入口 — main.cpp](#1-程序入口--maincpp)
- [协议定义 — packdef.h](#2-协议定义--packdefh)
- [业务层 — Kernel](#3-业务层--kernel)
- [网络层 — netWork（Epoller / Session / Reactor）](#4-网络层--networkepoller--session--reactor)
- [数据库层 — CMySQL](#5-数据库层--cmysql)
- [数据库结构](#6-数据库结构)
- [数据流总览](#7-数据流总览)

---

## 0. 构建与运行

**构建系统：** CMake（无 Makefile、无 `.pro`/`.pri`，qmake 产物已删除）。

```bash
cd Server
cmake -S . -B build && cmake --build build
```

**关键编译配置（`CMakeLists.txt`）：**

| 项 | 值 | 说明 |
|---|---|---|
| 编译标准 | C++17（`CMAKE_CXX_STANDARD 17`） | 代码用到 `std::filesystem` / `std::string_view` |
| 编译参数 | `-Wall -Wextra -D_FILE_OFFSET_BITS=64` | 64 位文件偏移，支持大文件 |
| 线程库 | `find_package(Threads REQUIRED)` → `Threads::Threads` | `std::thread` |
| MySQL | `find_path` / `find_library` 定位 `mysql/mysql.h` 与 `libmysqlclient` | 需安装 `libmysqlclient-dev` |

**源码文件（6 个）：** `main.cpp`、`netWork/epoller.cpp`、`netWork/session.cpp`、`netWork/reactor.cpp`、`CMySQL/cmysql.cpp`、`Kernel/kernel.cpp`。

**运行前置条件：**
1. 执行 `sql/init.sql` 初始化 MySQL 数据库 `server`；
2. 设置环境变量 `MYSQL_PASSWORD`（数据库 root 密码，未设置则启动失败）；
3. 启动 `./build/Server`，回车或 `Ctrl+C` 优雅停止。

---

## 1. 程序入口 — main.cpp

**文件路径:** `Server/main.cpp`

**已去除 Qt**：不再有 `QCoreApplication`，改用 `<csignal>` + `<sys/select.h>`（stdin 轮询）。

### 进程 / 线程模型

```
主线程（main.cpp）                       Reactor 线程（由 kernel::open() 内部启动）
 ├─ signal(SIGINT/SIGTERM, OnSignal)      ├─ epoll_wait 事件循环
 ├─ kernel::GetKernel().open()            │   ├─ accept / EPOLLIN / EPOLLOUT / EPOLLERR
 │    ├─ 建存储目录 disk_file/            │   └─ 就地调用 kernel::dealData() 执行业务
 │    ├─ 读环境变量 MYSQL_PASSWORD        │
 │    ├─ 连接 MySQL                       │
 │    └─ InitNetWork() 启动 Reactor 线程  │
 ├─ select() 轮询 stdin（100ms 超时）     │
 └─ kernel::GetKernel().close()           └─ 退出标志置 false → join
```

### 关键代码

```cpp
std::atomic<bool> g_stop{false};                 // 信号处理器只置此标志（async-signal-safe）
void OnSignal(int) { g_stop = true; }            // SIGINT/SIGTERM 处理

int main() {
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
    if (!kernel::GetKernel().open()) { ... return 1; }

    while (!g_stop) {
        // select 轮询 stdin：读到回车即退出；100ms 超时后检查 g_stop
        fd_set readfds; FD_ZERO(&readfds); FD_SET(STDIN_FILENO, &readfds);
        timeval timeout{0, 100000};
        if (select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &timeout) > 0) {
            char c; if (read(STDIN_FILENO, &c, 1) > 0) break;
        }
    }
    kernel::GetKernel().close();
}
```

### 设计要点

- **SIGINT/SIGTERM** 处理器只做原子标志置位（不调用 `printf`/`malloc` 等非 async-signal-safe 函数），真正的收尾逻辑在主循环轮询后执行；
- **SIGPIPE** 无需在此捕获——网络层发送用 `MSG_NOSIGNAL` 抑制（见 `session.cpp`）；
- stdin 用 `select()` 轮询而非阻塞 `getchar()`，保证在等待回车期间仍能响应退出信号。

---

## 2. 协议定义 — packdef.h

**文件路径:** `Server/packdef.h`

> 两端 `packdef.h` 必须逐字一致。本版本已完成 **Phase 0 定长化**：3 处跨平台 `long` 字段统一改为 `int32_t`。

### 包类型编号

所有数据包通过首字节 `m_nType` 分发，值域来源于预处理器宏：

```
_default_protocol_base = 0          (基准值)
  ├── register_rq/rs         = 1/2   注册
  ├── login_rq/rs            = 3/4   登录
  ├── getfilelist_rq/rs      = 5/6   获取文件列表
  ├── uploadfileinfo_rq/rs   = 7/8   上传文件元信息
  ├── uploadfilecontent_rq/rs= 9/10  上传文件内容（10 号响应当前未实现）
  ├── deletefile_rq/rs       = 11/12 删除文件（12 号响应当前未实现）
  ├── downloadfileinfo_rq/rs = 13/14 下载文件（下载正文复用 14 号响应）
  ├── downfilecontent_rq/rs  = 15/16 预留编号，当前业务未使用
  ├── selectfile_rq/rs       = 17/18 搜索文件
  ├── sharelink_rq/rs        = 19/20 分享文件
  └── getlink_rq/rs          = 21/22 提取分享
```

### 定宽常量

```cpp
#define MAX_SIZE   50     // 定长字符字段，含末尾 '\0'
#define FILE_NUM   15     // 列表/搜索每页最多 15 条
#define SQLLEN     1024   // 历史 SQL 缓冲区大小（当前用 std::string 动态拼装，未使用）
#define FILE_PATH  260    // 路径缓冲区大小
#define ONE_PAGE   4096   // 上传/下载固定分块缓冲区字节数
```

### 关键数据结构

#### 包基类 — `STRU_BASE`

```cpp
struct STRU_BASE {
    char m_nType = _default_protocol_base;  // 首字节即协议号，接收方据此直接分发
};
```

所有请求/响应结构体继承自它，构造函数把 `m_nType` 赋为对应宏，定长数组成员用 `{}` 默认清零。

#### 类型宽度约定（跨平台关键）

| 字段 | 类型 | 说明 |
|---|---|---|
| 用户 ID / 文件 ID / 文件大小 | `long long` | 两端 x86-64 皆为 8 字节，安全 |
| 上传正文块有效长度 `m_fileNum` | `int32_t` | 原为 `long`，Windows 4B / Linux 8B 会破坏协议，已定长化 |
| 下载块有效长度 `m_fileNum` | `int32_t` | 同上 |
| 搜索页条数 `szFileNum` | `int32_t` | 同上 |

> 结构体只允许 `char` 数组与定宽整数成员，禁止 `std::string`/`vector` 等类类型成员——协议依赖相同的编译器、字节序、类型宽度与对齐规则。

#### 关键业务包示例

| 包名 | 用途 | 关键字段 |
|------|------|----------|
| `STRU_REGISTER_RQ` | 注册请求 | `szName[50]`, `szpassword[50]`, `szTel` (`long long`) |
| `STRU_REGISTER_RS` | 注册响应 | `szResult` |
| `STRU_LOGIN_RQ` | 登录请求 | `szName[50]`, `szpassword[50]` |
| `STRU_LOGIN_RS` | 登录响应 | `szResult`, `szUserId` (`long long`) |
| `STRU_GETFILELIST_RS` | 列表响应 | `arrFileInfo[15]`(`FileInfo`), `szFileNum` (`long long`) |
| `STRU_UPLOADFILEINFO_RQ` | 上传元信息 | `UserId`, `szFileName`, `szFilesize`, `szFileMD5[50]` |
| `STRU_UPLOADFILEINFO_RS` | 上传元信息响应 | `szFileMD5`, `fileid`, `m_pos`(断点), `m_Result` |
| `STRU_UPLOADFILECONTENT_RQ` | 上传正文块 | `userid`, `fileid`, `m_FileContent[4096]`, `m_fileNum`(`int32_t`) |
| `STRU_DOWNLOADFILE_RS` | 下载正文块 | `m_FileContent[4096]`, `m_fileNum`(`int32_t`) |
| `STRU_SELECTFILE_RS` | 搜索响应 | `arrFileInfo[15]`, `szFileNum`(`int32_t`) |
| `STRU_SHARELINK_RS` | 分享响应 | `szFileName`, `szCode[50]`(4 位提取码) |
| `STRU_GETLINK_RS` | 提取响应 | `szFileName`, `szFileUploadTime`, `szFileSize`, `szResult` |

> `FileInfo` 为列表/搜索/提取响应复用的文件摘要结构，含 `szFileName` / `szFileUploadTime` / `szFileSize`，不暴露服务端物理路径与文件 ID。

### 结果码常量

```cpp
// 注册
_register_res_failed  = 0   // 失败（用户名/手机号重复或落库失败）
_register_res_success = 1   // 成功

// 登录
_login_res_failed  = 0      // 密码错误
_login_res_noexist = 1      // 用户不存在
_login_res_success = 2      // 登录成功

// 上传协商
_uploadfileinfo_repeat    = 0  // 重复上传（同用户同 MD5 且磁盘已完整）
_uploadfileinfo_continue  = 1  // 断点续传（返回 m_pos）
_uploadfileinfo_flashtrans= 2  // 秒传（他用户已有相同 MD5，引用计数 +1）
_uploadfileinfo_normal    = 3  // 正常传输（从头发送）
_uploadfileinfo_failed    = 4  // 失败（默认值，覆盖所有提前返回路径）

// 提取
_getlink_failed  = 0          // 失败（含提取自己的分享码、无效码、事务失败）
_getlink_success = 1          // 成功
```

---

## 3. 业务层 — Kernel

**文件路径:** `Server/Kernel/`

### 接口 — IKernel.h

```cpp
using SOCKET = int;   // Linux 下套接字描述符就是 int，保留别名最小化改动

class IKernel {
public:
    virtual bool open() = 0;                                        // 建目录 + 连库 + 监听
    virtual void close() = 0;                                       // 停网络 + 清任务 + 断库
    virtual void dealData(SOCKET socketWaiter, const char* szbuf, int len) = 0; // 分发入口
};
```

**接口隔离：** 网络层只依赖本契约，收包代码无需了解注册/上传/数据库细节；网络模型可在不改 kernel 的前提下替换（select / epoll / io_uring）。

### 实现 — kernel（Meyers 单例）

```cpp
class kernel : public IKernel {
private:
    std::unique_ptr<INet> m_pNet;    // 实际指向 Reactor
    std::unique_ptr<CMySql> m_pSql;  // MySQL C API 封装
    std::string m_systemPath;        // 实体文件存储根目录
    std::map<std::pair<long long, long long>, std::unique_ptr<fileinfo>> m_uploads; // 活动上传任务
};
```

**Meyers 单例（替代原饿汉式）：**

```cpp
kernel& kernel::GetKernel() {
    static kernel instance;   // C++11 起静态局部变量初始化线程安全
    return instance;
}
```

**无锁并发模型：** 所有请求处理都在 Reactor 线程内被调用（单线程），因此 `m_uploads`、`m_pSql` 全程无锁——原「多线程共享单 MYSQL 连接」的隐患被单线程模型天然消除。

**存储路径硬编码：** `m_systemPath = "/home/zenith/workspace/Online_storage/disk_file/"`（构造函数中），每位用户以 ID 建子目录存放实体文件。

### 活动上传任务 — fileinfo

```cpp
struct fileinfo {
    std::unique_ptr<FILE, FileCloser> pFile;  // RAII 文件句柄
    long long userId;   // 上传发起人，阻止跨用户写
    long long fileId;   // file 表主键
    long long fileSize; // 元数据声明总大小，拒绝越界写入
    long long pos;      // 已写入字节数 = 断点续传起点
};
```

正文包不重复携带路径与总大小，服务端通过 `m_uploads`（键为 `(uid, fid)`）校验归属、限制写入边界、维护续传偏移。`FileCloser` 为 `FILE*` 的函数对象 deleter，使 `make_unique<fileinfo>()` 可行。

### 生命周期 — open() / close()

```cpp
bool kernel::open() {
    std::filesystem::create_directory(m_systemPath);   // (1) 建存储目录
    const char* pwd = std::getenv("MYSQL_PASSWORD");   // (2) 密码经环境变量读取
    if (!pwd) return false;
    if (!m_pSql->ConnectMySql("127.0.0.1", "root", pwd, "server")) return false; // (3) 连库
    if (!m_pNet->InitNetWork()) { m_pSql->DisConnect(); return false; }          // (4) 监听
    return true;
}

void kernel::close() {
    m_pNet->UnitNetWork();   // 先停网络，确保清理任务表时无新正文包并发进入
    m_uploads.clear();       // unique_ptr<FILE> 随容器销毁自动 fclose，部分文件供下次续传
    m_pSql->DisConnect();
}
```

### 请求分发 — dealData()

采用 **两级校验**：首字节选择候选结构体 → 校验精确包长、字符串终止符、关键数值范围，再调用对应处理函数。

```
szbuf[0] (m_nType)
  ├── 1  → RegisterRq()          注册
  ├── 3  → LoginRq()             登录
  ├── 5  → GetFileLisRq()        文件列表
  ├── 7  → UploadFileLisRq()     上传元信息（去重协商）
  ├── 9  → UploadFileContentRq() 上传正文块（无响应）
  ├── 11 → DeleteFileRq()        删除（无响应）
  ├── 13 → DownLoadFileRq()      下载（分块回包）
  ├── 17 → SelectFileRq()        搜索
  ├── 19 → ShareLinkRq()         分享
  └── 21 → GetLinkRq()           提取分享
```

**安全校验要点（匿名命名空间辅助函数）：**

| 辅助函数 | 作用 |
|---|---|
| `HasTerminator<T>()` | 定长字符数组须含 `'\0'`，否则 `strlen`/转义会越界读不可信内存 |
| `IsSafeFileName()` | 拒绝 `.`/`..`、`\/:*?"<>|` 及控制字符，防目录穿越 |
| `GetDiskFileSize()` | 用 `std::filesystem::file_size` 的 error_code 重载，不抛异常 |
| `CopyToArray<N>()` | 用 `snprintf` 安全拷贝进定长字段（替代 MSVC `strcpy_s`） |

### 各业务函数详解

#### RegisterRq() — 用户注册

```
流程:
 ├── (1) EscapeString 转义用户名/密码（防 SQL 注入）
 ├── (2) INSERT INTO user (u_name, u_password, u_tel)
 ├── (3) LastInsertId() 取自增 u_id
 ├── (4) create_directory(disk_file/<u_id>) 建用户目录
 │        └── 失败 → DELETE 刚插入的记录（补偿，避免无存储空间的无效账号）
 └── (5) sendData() 返回 STRU_REGISTER_RS
```

#### LoginRq() — 用户登录

```
流程:
 ├── (1) SELECT u_id, u_password FROM user WHERE u_name = ?
 ├── (2) 无记录 → _login_res_noexist
 ├── (3) 密码不匹配 → _login_res_failed（szUserId 保持 0）
 └── (4) 匹配 → _login_res_success + 返回 u_id
```

#### GetFileLisRq() — 文件列表

```
流程:
 ├── (1) SELECT f_name, f_size, f_uploadtime FROM ufile WHERE u_id = ?
 ├── (2) 每装满 FILE_NUM 条或耗尽结果集 → 发送一页 STRU_GETFILELIST_RS
 ├── (3) 发送后逐元素清空数组槽，防泄漏上一页数据
 └── 客户端把连续页追加到表格
```

#### UploadFileLisRq() — 上传元信息处理（核心去重逻辑）

```
按 MD5 查询 ufile 视图（u_id, f_id, f_path, f_size）
 ├── 同用户拥有相同 MD5
 │   ├── 存在活动任务 → _continue（返回精确 m_pos）
 │   ├── 磁盘大小 == 声明大小 → _repeat（重复上传）
 │   └── 磁盘不完整（重启后任务表丢失）→ 重建任务（"ab" 续传 / "wb" 截断重传）
 ├── 他用户拥有相同 MD5 且磁盘完整 → 记 reusableFileId 供秒传
 ├── md5Exists 且 reusableFileId > 0 → 秒传：事务内 f_count+1 + INSERT user_file
 └── 全新内容 → 建用户目录 + fopen("wb") 空文件 + 事务写 file/user_file + 建活动任务
```

**事务与磁盘回滚：** 秒传/新建的「引用计数 +1 + 映射插入」必须原子提交，任一失败 `rollback`；新建的空文件在事务失败时 `fclose` + `remove` 回滚磁盘副作用。零字节文件创建后即完整，不保留活动任务。

#### UploadFileContentRq() — 接收文件内容块（无响应包）

```
流程:
 ├── (1) 强转为 STRU_UPLOADFILECONTENT_RQ*
 ├── (2) m_uploads.find({userid, fileid}) 定位任务（阻止跨用户写）
 ├── (3) 校验 m_fileNum ∈ [1, ONE_PAGE] 且 pos + m_fileNum ≤ fileSize
 ├── (4) fwrite() 写文件，以返回值累加 pos
 └── (5) pos == fileSize → m_uploads.erase()（unique_ptr 自动 fclose）
```

#### DownLoadFileRq() — 下载文件（分块回包）

```
流程:
 ├── (1) SELECT f_path FROM ufile WHERE u_id = ? AND f_name = ?（验证归属）
 ├── (2) std::ifstream 二进制打开
 ├── (3) 每次读 ONE_PAGE 字节 → STRU_DOWNLOADFILE_RS（m_fileNum = gcount）
 └── (4) 读到 EOF 结束；sendData 失败（客户端断开）立即终止读取
```

> 协议无总大小/偏移/结束标记，客户端依赖顺序写入；下载正文复用 `_default_protocol_downloadfileinfo_rs`（14 号）。

#### DeleteFileRq() — 删除文件（无响应包）

```
流程:
 ├── (1) SELECT f_id, f_count, f_path FROM ufile WHERE u_id = ? AND f_name = ?
 ├── (2) DELETE FROM user_file（解除当前用户映射）
 ├── (3) f_count > 1 → UPDATE file SET f_count - 1（仅减引用计数）
 └── (4) f_count == 1 → DELETE FROM file + std::filesystem::remove（物理删除）
```

#### SelectFileRq() — 搜索文件

```
流程:
 ├── (1) EscapeString 转义关键字
 ├── (2) SELECT ... FROM ufile WHERE u_id = ? AND f_name LIKE '%keyword%'
 └── (3) 分页回包逻辑与文件列表一致（每页 FILE_NUM 条）
```

#### ShareLinkRq() — 文件分享

```
流程:
 ├── (1) std::mt19937 + uniform_int_distribution(0,35) 生成 4 位 36 进制提取码
 │        （替代旧式 srand/rand，无偏随机源）
 ├── (2) 验证用户拥有该文件（ufile 查询 f_id）
 ├── (3) INSERT INTO user_shared(uid, fid, code)
 │        └── 插入失败 → 查询该用户该文件已有分享码并复用（重复分享返回同一码）
 └── (4) 返回 STRU_SHARELINK_RS（szFileName + szCode）
```

#### GetLinkRq() — 提取分享

```
流程:
 ├── (1) SELECT uid, fid FROM user_shared WHERE code = ?
 ├── (2) uid == 请求者 → _failed（提取自己的分享码不建新映射）
 ├── (3) uid != 请求者 → 查文件摘要 + 事务：INSERT user_file + f_count+1
 │        └── 任一失败 rollback（防去重引用失真）
 └── (4) 返回 STRU_GETLINK_RS
```

---

## 4. 网络层 — netWork（Epoller / Session / Reactor）

**文件路径:** `Server/netWork/`

### 接口 — INet.h

```cpp
using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;
constexpr int     SOCKET_ERROR   = -1;

class INet {
public:
    virtual bool InitNetWork(unsigned long dwip = 0, short nport = 8899) = 0;
    virtual void UnitNetWork() = 0;
    virtual bool sendData(SOCKET sockWaiter, const char* szbuf, int nLen) = 0;
    virtual void recvData() = 0;   // epoll 版由 run() 事件循环集中执行，空实现
};
```

**依赖倒置：** kernel 只依赖本抽象，网络模型可在不改 kernel 的前提下替换。当前实现为单线程 epoll Reactor。

### 4.1 Epoller — epoll 实例 RAII 封装

```cpp
class Epoller {
public:
    Epoller();   // epoll_create1(EPOLL_CLOEXEC)
    ~Epoller();  // close(m_epfd)
    bool add(int fd, uint32_t events, void* ptr);  // EPOLL_CTL_ADD
    bool mod(int fd, uint32_t events, void* ptr);  // EPOLL_CTL_MOD
    bool del(int fd);                              // EPOLL_CTL_DEL
    int  wait(std::vector<epoll_event>& events, int timeoutMs); // epoll_wait
private:
    int m_epfd;   // -1 表示创建失败
};
```

**要点：**
- `EPOLL_CLOEXEC` 使描述符在 exec 时自动关闭，防子进程泄漏；
- **边缘触发（EPOLLET）**：仅在 fd 状态由「不可读写」变为「可读写」时通知一次，使用方必须循环读写到 `EAGAIN`（见 Session/Reactor），否则残留数据不再触发新事件。

### 4.2 Session — 每连接解帧状态机 + 收发缓冲

```cpp
class Session {
public:
    using FrameHandler = std::function<void(int fd, const char* data, int len)>;
    Session(int fd, const sockaddr_in& addr, FrameHandler handler);
    ~Session();                            // close(fd)，RAII 释放
    bool handleRead();                     // recv 累积 + parseFrames
    bool enqueueWrite(const char* data, int len); // 追加写缓冲
    bool handleWrite();                    // 非阻塞 send 刷出写缓冲
    bool wantWrite() const;                // 是否还有未写完字节
private:
    bool parseFrames();                    // READ_HEADER(4B) → READ_BODY(len)
    int  m_fd; sockaddr_in m_addr; FrameHandler m_handler;
    std::vector<char> m_readBuffer;        // 半包缓冲
    int  m_expectSize = 0; bool m_readingHeader = true;
    std::string m_writeBuffer; size_t m_writeOffset = 0;
};
```

**长度前缀状态机：**

```
READ_HEADER（读满 4 字节包长）
    ↓ 校验 1 ≤ len ≤ MAX_PACKET_SIZE(1 MiB)
READ_BODY（累积 len 字节包体）
    ↓ 完整帧 → m_handler(fd, data, len) → 回调 kernel::dealData
    ↓ 回到 READ_HEADER
```

**要点：**
- **粘包 / 拆包**：一次 `recv` 可能含多帧（循环解析），一帧可能分多次到达（缓冲不足时等待下次 EPOLLIN）；
- **包长上限 1 MiB**：拒绝异常长度，防恶意客户端诱导无界申请堆内存（DoS）；
- `recv` 边缘触发下循环读到 `EAGAIN`；`n==0` 对端关闭，`ECONNRESET`/`EINTR` 等按连接错误关闭；
- `send` 用 `MSG_NOSIGNAL`：对端关闭返回 `EPIPE` 而非触发 SIGPIPE 终止进程；`EAGAIN` 表示内核发送缓冲满，剩余字节交 EPOLLOUT 续写。

### 4.3 Reactor — 单线程 epoll 事件循环

```cpp
class Reactor : public INet {
private:
    int  m_listenFd = -1;
    Epoller m_epoller;
    std::unordered_map<int, std::unique_ptr<Session>> m_sessions; // fd → Session
    std::thread m_thread;                    // Reactor 线程
    std::atomic_bool m_quitFlag{false};      // 跨线程退出开关
    void run();                              // 事件循环
    void handleAccept();                     // accept 到 EAGAIN
    void closeSession(Session* session);     // epoll DEL + erase（析构 close）
    static void setNonBlocking(int fd);      // fcntl O_NONBLOCK
};
```

**InitNetWork() 初始化流程：**

```
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
    ↓
setsockopt(SO_REUSEADDR)     ← 端口复用，便于快速重启
    ↓
setNonBlocking(listenFd)     ← 非阻塞是边缘触发与事件循环的前提
    ↓
bind(INADDR_ANY, 8899)
    ↓
listen(SOMAXCONN)
    ↓
m_epoller.add(listenFd, EPOLLIN | EPOLLET, nullptr)  ← 监听 fd 用 ptr==nullptr 标识
    ↓
m_quitFlag = true; m_thread = std::thread(&Reactor::run, this)
```

**run() 事件循环：**

```cpp
while (m_quitFlag) {
    int n = m_epoller.wait(events, 1000);   // 1s 超时轮询退出标志（避免引入 eventfd）
    for (每个就绪事件) {
        if (ptr == nullptr)       handleAccept();       // 监听 fd：accept
        else if (EPOLLERR|HUP)    closeSession(session);
        else if (EPOLLIN)         session->handleRead() // 读帧 → 就地调 kernel::dealData
        else if (EPOLLOUT)        session->handleWrite(); // 刷写缓冲，写尽摘除 EPOLLOUT
    }
}
```

**sendData()（长度前缀发送，处理 TCP 短写）：**

```cpp
bool Reactor::sendData(SOCKET sockWaiter, const char* szbuf, int nLen) {
    // 先入写缓冲「4 字节包长 + 包体」，再立即非阻塞刷出
    session->enqueueWrite(&nLen, sizeof(nLen));
    session->enqueueWrite(szbuf, nLen);
    session->handleWrite();              // 写不尽的余量挂 EPOLLOUT
    if (session->wantWrite())
        m_epoller.mod(sockWaiter, EPOLLIN | EPOLLOUT | EPOLLET, session);
}
```

> 仅被 kernel 在 Reactor 线程内（handler 回调中）调用，因此可无锁访问 `m_sessions`。

**UnitNetWork() 优雅关闭：**

```
m_quitFlag = false
    ↓
join()（事件循环靠 1s 超时轮询退出）
    ↓
遍历 m_sessions：epoll DEL + erase（Session 析构 close fd）
    ↓
epoll DEL + close(m_listenFd)
```

> 顺序不可颠倒：先 join 再关 fd——线程内正在 `epoll_wait` 的 fd 若被其它线程 close 属未定义行为。

**单线程取舍（TODO 注释）：** MySQL 慢查询/大块磁盘 IO 会短暂阻塞事件循环、拖慢所有连接；换来无锁与实现简洁（教学/本地规模可接受）。未来若要恢复线程池，只需把「完整帧 → handler」这一单一回调点改为投递任务队列，其余代码不动。

---

## 5. 数据库层 — CMySQL

**文件路径:** `Server/CMySQL/`

### 类定义

```cpp
class CMySql {
public:
    CMySql();     // mysql_init + 配置超时/字符集（RAII：unique_ptr<MYSQL, decltype(&mysql_close)>）
    ~CMySql();    // DisConnect → unique_ptr 析构调 mysql_close
    bool ConnectMySql(const char* ip, const char* user, const char* password, const char* db);
    void DisConnect();                                    // mysql.reset()
    std::vector<std::vector<std::string>> SelectMysql(const std::string& sql, int nColumn);
    bool UpdateMysql(const std::string& sql);
    std::string EscapeString(std::string_view value) const;  // 防 SQL 注入
    unsigned long long LastInsertId() const;
private:
    std::unique_ptr<MYSQL, decltype(&mysql_close)> mysql; // RAII 连接句柄
};
```

### 连接配置

```cpp
mysql_options(connection, MYSQL_OPT_CONNECT_TIMEOUT, &5); // 连接/读写超时 5s，防线程永久阻塞
mysql_options(connection, MYSQL_OPT_READ_TIMEOUT,    &5);
mysql_options(connection, MYSQL_OPT_WRITE_TIMEOUT,   &5);
mysql_options(connection, MYSQL_SET_CHARSET_NAME, "utf8mb4"); // 中文文件名/用户名可正确转义存储
```

### SelectMysql() — 查询

```cpp
mysql_query(mysql, sql);
MYSQL_RES* result = mysql_store_result(mysql);   // 结果缓存到客户端内存
while (row = mysql_fetch_row(result)) {
    // 每行一个 vector<string>（nColumn 个字段），SQL NULL 统一存为 "null"
}
mysql_free_result(result);   // 读完必须释放，否则每次查询泄漏结果集
```

**返回类型：** `vector<vector<string>>`，业务层按 `[行][列]` 读取（替代旧版扁平化 `list<string>`）。例如 `SelectMysql(sql, 3)` 后 `rows[0][0]` 取第一行第一列。

### EscapeString() — 转义

```cpp
// mysql_real_escape_string 按当前字符集转义；最坏每字节转义为两字节，缓冲预留 2×len+1
std::string escaped(inputLength * 2 + 1, '\0');
mysql_real_escape_string(mysql, escaped.data(), value.data(), inputLength);
```

**所有外部输入拼入 SQL 前必须经此转义**，否则改变语句结构导致注入。

### UpdateMysql() — 增删改 / 事务

```cpp
mysql_query(mysql, sql);   // 也用于 start transaction / commit / rollback，事务边界由业务函数编排
```

### 连接参数

| 项 | 值 |
|---|---|
| 地址 | `127.0.0.1` |
| 用户 | `root` |
| 密码 | 环境变量 `MYSQL_PASSWORD` |
| 数据库 | `server` |

---

## 6. 数据库结构

MySQL 数据库 `server`，建表脚本见 `sql/init.sql`（`utf8mb4` 字符集）。

| 表 | 主键 | 用途 | 关键字段 |
|------|:---:|------|------|
| `user` | u_id (自增) | 用户信息 | u_name (唯一), u_password, u_tel (唯一) |
| `file` | f_id (自增) | 文件实体 | f_name, f_size, f_uploadtime, f_path, f_count (引用计数), f_md5 |
| `user_file` | (u_id, f_id) | 用户-文件多对多映射 | u_id, f_id, time |
| `user_shared` | id (自增) | 分享链接 | uid, fid, code |
| `ufile` | — | JOIN 视图（user_file LEFT JOIN file） | 简化归属查询 |

---

## 7. 数据流总览

```
客户端 TCP 连接
    ↓ accept（非阻塞）
Reactor 注册 EPOLLIN | EPOLLET → 创建 Session
    ↓ EPOLLIN 就绪
Session::handleRead()：recv 循环读 → 累积读缓冲 → parseFrames()
    ↓ 切出完整帧（4 字节包长 + 包体）
m_handler(fd, data, len) → kernel::GetKernel().dealData()
    ↓ 两级校验（包长 / 终止符 / 数值范围 / 文件名合法性）
switch(m_nType) → 分发到具体 *Rq() 处理函数
    ↓
处理函数 → EscapeString → m_pSql->SelectMysql / UpdateMysql（含事务）
    ↓
处理函数 → 构建响应结构体 → m_pNet->sendData()
    ↓
Session::enqueueWrite（长度前缀 + 包体）→ handleWrite() 非阻塞刷出
    ↓ 写不尽
挂 EPOLLOUT → 事件循环后续触发 handleWrite() 续写 → 写尽摘除 EPOLLOUT
```

**异常路径：** `EPOLLERR | EPOLLHUP`、`recv` 返回 0（对端关闭）、非法包长（`<=0` 或 `>1 MiB`）→ `closeSession()`（epoll DEL + 销毁 Session 触发 close fd）。
