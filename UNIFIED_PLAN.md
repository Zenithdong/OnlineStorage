# Online_storage 整合改造计划（两端 Linux 化 + 服务端去 Qt epoll + C++17）

> **定位**：本文件整合并**取代**根目录 `MODERN_CPP_PLAN.md`、`MIGRATION_PLAN.md` 与
> `Server/LINUX_REACTOR_PLAN.md` 三份规划，落实已确认方向：
>
> - **客户端**：保留 Qt（Qt6 Widgets），**Linux 化**（Winsock → BSD socket，不保留 Windows 分支）。
> - **服务端**：**彻底去 Qt**，Linux 单线程 epoll Reactor 重写（纯 C++）。
> - **语言标准**：统一 **C++17** 编译，主体用 **C++11/14**，允许少量高收益 C++17
>   （`std::scoped_lock`、`std::string_view`、`std::filesystem`、`std::is_trivially_copyable_v`）。
> - **二进制协议与业务逻辑不变**（11 对请求/响应、MD5 秒传、断点续传、分享引用计数等语义全部保留）。

---

## 0. 目标与红线

### 0.1 目标

| 维度 | 现状 | 目标 |
|------|------|------|
| 平台 | Windows（Winsock + MinGW + MSVC CRT） | 两端 Linux x86-64 |
| 客户端 | Qt 6.11 + Winsock + `CreateThread` | Qt 6 + BSD socket + `std::thread` + 智能指针 |
| 服务端 | Qt Core + Winsock `select()` | **无 Qt** + epoll 单线程 Reactor + POSIX/`std::filesystem` |
| 语言 | C++17（混用 C 风格） | C++17，主体 C++11/14 现代特性 |
| 协议 | 3 处 `long`（跨平台宽度不稳定） | 全部定宽 `int32_t`，两端逐字一致 |

### 0.2 三大红线（每一步都必须遵守）

1. **协议布局不变**：`packdef.h` 结构体只含 char 数组与定宽整数，**禁止引入
   `std::string`/`std::vector`/`std::array` 等类类型成员**；唯一允许的成员类型改动是
   3 处 `long → int32_t`。
2. **两端 `packdef.h` 逐字一致**：改完一端复制到另一端，`diff` 必须为空。
3. **每阶段保持可编译可运行**：先机械替换、后结构改造；每个阶段结束各编译一次
   （服务端 `make`，客户端 `qmake && make`）。

---

## 1. 语言标准与 C++17 特性取舍

两端编译标准统一为 `-std=c++17`（客户端 `CONFIG += c++17`，服务端 Makefile/CMake
`cxx_std_17`）。**代码主体只用 C++11/14**，C++17 专属特性按“高收益、零依赖”原则限量引入。

| 特性 | 标准 | 使用 | 说明 / 替换 |
|------|------|------|-------------|
| `unique_ptr` / `shared_ptr` | C++11 | ✅ 主力 | 替换所有裸 `new`/`delete` |
| `std::make_unique` | C++14 | ✅ 主力 | |
| `lambda` / `auto` / range-for / `enum class` | C++11 | ✅ 主力 | |
| `std::thread` / `std::atomic` / `std::function` | C++11 | ✅ 主力 | 替换 `CreateThread`/`HANDLE` |
| `std::mt19937` + `uniform_int_distribution` | C++11 | ✅ 主力 | 替换 `srand`/`rand` |
| `std::array` | C++11 | ✅ 主力 | MD5 定宽成员/表 |
| `=default` / `=delete` / `constexpr` / `static_assert` | C++11 | ✅ 主力 | |
| `std::scoped_lock` | C++17 | ⚪ 限量 | 替换 `lock_guard`（**仅客户端**；服务端单线程无锁） |
| `std::string_view` | C++17 | ⚪ 限量 | CMySQL `EscapeString` 参数 |
| `std::filesystem` | C++17 | ⚪ 限量 | 目录/文件元数据（两端） |
| `std::is_trivially_copyable_v` | C++17 | ⚪ 限量 | `packdef.h` 布局自检 |
| 结构化绑定 / `if constexpr` / `optional` / `variant` / CTAD / `from_chars` | C++17 | ❌ 禁用 | 保持主体 C++11/14；`from_chars` 用 `std::stoll/stoi` 替代 |

> 说明：服务端 Reactor 单线程内完成 IO + 业务，全程无锁，故服务端**不需要** `scoped_lock`；
> `scoped_lock` 只出现在客户端网络层的锁场景。`std::filesystem` 在 GCC 15 下无需额外链接库。

---

## 2. 共享协议头 `packdef.h`（Phase 0，两端同步，最高优先级）

不依赖 MySQL 库，**现在即可做**，且必须最先做（否则中途协议不一致，上传/下载/搜索必挂）。

| 位置（两端 L 号一致） | 现状 | 改为 |
|------|------|------|
| 顶部 include 区 | — | `#include <cstdint>`、`#include <type_traits>` |
| L180 `STRU_UPLOADFILECONTENT_RQ` | `long m_fileNum = 0;` | `int32_t m_fileNum = 0;` |
| L198 `STRU_SELECTFILE_RS` | `long szFileNum = 0;` | `int32_t szFileNum = 0;` |
| L255 `STRU_DOWNLOADFILE_RS` | `long m_fileNum = 0;` | `int32_t m_fileNum = 0;` |
| 每个结构体后（可选增强） | — | `static_assert(std::is_trivially_copyable_v<T>);` `static_assert(sizeof(T) == 当前值);`（禁止断言 `is_standard_layout`，成员分布在基类+派生类会失败） |
| 协议号/尺寸/结果码宏（可选增强） | `#define` | `inline constexpr`（常量替换宏） |

- L146 `STRU_SELECTFILE_RQ::szFileNum` 是 `long long`（8 字节，跨平台稳定），**无需改**。
- 客户端调用点同步：`Client/mainwindow.cpp` 上传循环 `scr.m_fileNum = (long)readNum;`
  → `= static_cast<int32_t>(readNum);`（`readNum <= ONE_PAGE`，安全）。
- 改完后 `diff Client/packdef.h Server/packdef.h` 必须为空。

---

## 3. 服务端：去 Qt + epoll Reactor + 现代化

### 3.1 文件与目录规划

```
Server/
├── main.cpp                     #【改】去 QCoreApplication；回车 + SIGINT 优雅退出
├── packdef.h                    #【改】3 处 long → int32_t（与 Client 同步）
├── Kernel/
│   ├── IKernel.h                #【改】去 winsock2.h；SOCKET → int
│   ├── kernel.h / kernel.cpp    #【改】Meyers 单例、智能指针、filesystem、map 上传表、string SQL
├── netWork/
│   ├── INet.h                   #【改】去 winsock2.h；SOCKET → int
│   ├── epoller.h/.cpp           #【新】epoll RAII 封装（约 60 行）
│   ├── session.h/.cpp           #【新】每连接解帧状态机 + 读写缓冲
│   └── reactor.h/.cpp           #【重写】原 tcpnet.* 替换为 epoll 事件循环 + 连接管理
├── CMySQL/
│   └── cmysql.h/.cpp            #【改】mysql/mysql.h、qDebug→std::cerr、unique_ptr、vector 结果集
├── Makefile                     #【新】Linux 手写版
├── CMakeLists.txt               #【新】Linux 版
└── (删除) Server.pro、3 个 .pri、qmake 生成物、旧 release/
```

### 3.2 `main.cpp`（去 Qt）

- 删 `QCoreApplication`；删 `application.exec()` 与 EOF 回退分支（那是 Qt 事件循环的补偿逻辑，去 Qt 后不再需要）。
- 保留 `getchar()` 等回车退出；增加 `SIGINT`/`SIGTERM` 处理（`std::signal` + `std::atomic<bool>` 标志 → 主循环退出 → `kernel::GetKernel().close()`）。
- 单例调用由 `kernel::GetKernel()->open()` 改为 `kernel::GetKernel().open()`（Meyers 单例返回引用）。

### 3.3 接口层 `IKernel.h` / `INet.h`（去 winsock2）

- 删除 `#include <winsock2.h>`，改 `#include <sys/socket.h>` + `<netinet/in.h>` + `<arpa/inet.h>` + `<unistd.h>`。
- 因**不再保留 Windows 分支**，直接 `using SOCKET = int;`、`constexpr SOCKET INVALID_SOCKET = -1;`
  `constexpr int SOCKET_ERROR = -1;`（无 `#ifdef _WIN32`，业务层 `SOCKET` 参数零改动）。
- 两个接口头部的注释同步去掉“Winsock / IOCP”字样。

### 3.4 `Epoller`（新）

- `epoll_create1(EPOLL_CLOEXEC)` 构造、析构 `close`；`add`/`mod`/`del(fd, events, ptr)`；
  `wait(events, maxevents, timeout)`。
- **采用 ET 边缘触发**：读循环到 `EAGAIN`、写循环到 `EAGAIN`（LT 作为降级备选写进注释）。
- `epoll_wait` 超时取 1000ms；停机标志靠超时轮询检查，**不引入 eventfd**（单线程无跨线程唤醒需求）。

### 3.5 `Session`（连接状态机）

```
READ_HEADER(4B) → READ_BODY(len) → 投递完整帧 → 回到 READ_HEADER
```

- 读缓冲 `vector<char>` 累积半包，正确拆解**粘包/半包**；长度前缀校验 `1..MAX_PACKET`（防脏数据）。
- 写缓冲 `string` + 已写偏移；发送先尝试非阻塞 `write`，写不完的余量入缓冲并挂 `EPOLLOUT`，
  `EPOLLOUT` 触发后继续刷，写尽摘除。
- 成员：`fd`、对端地址、状态机阶段、已读字节数；生命周期由 Reactor 的
  `unordered_map<int, Session*>` 管理。

### 3.6 `Reactor`（替换原 `TCPNet`）

- 沿用 `INet` 接口：`InitNetWork` / `UnitNetWork` / `sendData` / `recvData`（`SOCKET` 已是 `int`）。
- 监听：`socket(AF_INET)` → `SO_REUSEADDR` → `bind(0.0.0.0:8899)` → `listen(SOMAXCONN)` → 非阻塞。
- `run()` 主循环：`epoll_wait` → 分发 `EPOLLIN`（listen=accept / 连接=读帧并就地调 `kernel::dealData`）、
  `EPOLLOUT`（刷写缓冲）、`EPOLLERR|EPOLLHUP`（关闭连接）。
- 停机：`std::atomic<bool>` 标志 + 每次 `wait` 超时后检查；`UnitNetWork` 置标志 → `join` → 关闭全部连接。
- 线程：`m_thread = std::thread(&Reactor::run, this);`，析构前 `joinable()` + `join`。
- **handler 单点投递**：完整帧处统一经一个 `std::function` 投递给 `kernel::dealData`，为将来接入
  线程池留口（当前单线程直接调用）。

### 3.7 `kernel` 业务层

| 维度 | 现状 | 改为 |
|------|------|------|
| 单例 | 饿汉 `static kernel* m_pKernel` + `new kernel` | Meyers：`kernel& kernel::GetKernel() { static kernel instance; return instance; }`；删静态成员与手动 `new` |
| 资源成员 | `INet* m_pNet; CMySql* m_pSql;` | `std::unique_ptr<INet> m_pNet; std::unique_ptr<CMySql> m_pSql;`（`std::make_unique`） |
| 存储路径 | `char m_szSystemPath[FILE_PATH]` | `std::string m_systemPath = "/home/zenith/workspace/Online_storage/disk_file/";`（项目当前 `disk_file/`，**以 `/` 结尾**，拼接处直接追加用户 ID，沿用无配置文件风格） |
| 上传任务表 | `list<fileinfo*> m_lstFileInfo` | `std::map<std::pair<long long,long long>, std::unique_ptr<fileinfo>> m_uploads;`（`pair` 自带 `operator<`） |
| `fileinfo` | `FILE* pFile;` | `std::unique_ptr<FILE, decltype(&fclose)> pFile;`（保留 `fwrite` 精确进度语义）；其余成员 `= 0` 默认初始化 |
| 析构 | 手工 `delete`/`fclose` | `~kernel() = default;` |
| 目录操作 | `CreateDirectoryA` + `GetLastError` | `std::filesystem::create_directory(path, ec)` + `ec` 判断（`exists` 视为成功） |
| 文件存在/删除 | `GetFileAttributesA` / `DeleteFileA` | `std::filesystem::exists` / `std::filesystem::remove` |
| 文件大小 | `fopen` + `_fseeki64` + `_ftelli64` | `std::filesystem::file_size(path, ec)`（`GetDiskFileSize` 可整体删除） |
| 下载读文件 | `fopen("rb")` + `fread` + `fclose` | `std::ifstream in{path, std::ios::binary}; while (in.read(buf, ONE_PAGE), (num = in.gcount()) > 0)` |
| 上传写文件 | `fopen("ab"/"wb")` | `pFile.reset(std::fopen(path, "ab"))`（写入仍用 `fwrite` 保精确字节数） |
| SQL 构造 | `char szsql[SQLLEN]` + `snprintf` | `std::string sql = "select ... where u_name = '" + EscapeString(userName) + "'";` 数值用 `std::to_string`；LIKE 的 `%` 属格式串写死 |
| 数值解析 | `_atoi64` / `atoll` / `atoi` | `std::stoll` / `std::stoi` |
| 字符串拷贝 | `strcpy_s` ×11 | 匿名 namespace 内 `CopyToArray` 模板（`std::strncpy` + 末尾置 `'\0'`） |
| 内存清零 | `ZeroMemory(x, sizeof)` | 逐元素重置：`for (auto& info : arr) info = {};`（**禁止**整体 `xxx = {};`，会误清 `m_nType`） |
| 随机码 | `srand(time)` + `rand()%36` | `std::mt19937 engine{std::random_device{}()}; std::uniform_int_distribution<int> dist(0,35);` 查表 |
| 强转 | `(STRU_*_RQ*)szbuf` | `reinterpret_cast<const STRU_*_RQ*>(szbuf)` |
| `dealData` 签名 | `SOCKET socketWaiter` | `int socketWaiter`（见 3.3） |
| `(void)socketWaiter` 消音 | `UploadFileContentRq`/`DeleteFileRq` 开头 | 无名形参 |

> 服务端单线程 Reactor 内完成 MySQL + 磁盘 IO，**全程无锁**：连接表、收发缓冲、`m_uploads`、
> 单 `MYSQL` 连接只被 Reactor 线程访问，无需任何互斥锁（原“每连接一线程 + 共享单 MYSQL 连接”
> 的线程安全隐患被单线程模型直接消除）。

### 3.8 `CMySQL`

| 维度 | 现状 | 改为 |
|------|------|------|
| 头文件 | `mysql.h`（Windows 路径） | `#include <mysql/mysql.h>` |
| 日志 | `qDebug` ×4 | `std::cerr`（顺带完成去 Qt） |
| 句柄 | `MYSQL* mysql;` | `std::unique_ptr<MYSQL, decltype(&mysql_close)> mysql;` |
| `SelectMysql` | `bool SelectMysql(const char*, int, list<string>&)` | `std::vector<std::vector<std::string>> SelectMysql(const std::string&, int);`（失败返回空 `vector`） |
| `UpdateMysql` | `bool UpdateMysql(const char*)` | `bool UpdateMysql(const std::string&)`（内部 `c_str()`） |
| `EscapeString` | `string EscapeString(const char*) const` | `std::string EscapeString(std::string_view) const` |
| `using namespace std` | 有 | 删除，全文 `std::` 限定 |
| 锁 | 共享连接需加锁 | 单线程模型**去锁** |

---

## 4. 客户端：保留 Qt + Linux 化 + 现代化

### 4.1 网络层 `Tcpnet/tcpnet.{h,cpp}`

| 维度 | 现状 | 改为 |
|------|------|------|
| include | `winsock2.h` / `windows.h` | `sys/socket.h netinet/in.h arpa/inet.h unistd.h cerrno thread cstring` |
| 初始化/清理 | `WSAStartup`/`WSACleanup`/`m_bWsaStarted`/`LOBYTE`/`HIBYTE` | 全部删除，`ConnectServer` 直接 `socket()` |
| 地址解析 | `S_un.S_addr` / `inet_addr` | `serverAddress.sin_addr.s_addr` + `inet_pton(AF_INET, szip, ...)`（失败返回 false） |
| 超时 | `SO_SNDTIMEO` 传 `DWORD` 毫秒 | `struct timeval sendTimeout{5, 0};` |
| 线程 | `HANDLE m_hThread` + `CreateThread` | `std::thread m_thread` + `static void ThreadRecv(tcpnet* self);` `m_thread = std::thread(&tcpnet::ThreadRecv, this);` |
| 线程退出 | `WaitForSingleObject` + `CloseHandle` | `if (m_thread.joinable()) m_thread.join();`（保持“清标志 → `shutdown` 唤醒 `recv` → `join`”顺序） |
| 收包缓冲 | `new char[]` / `delete[]` ×2 | `auto packet = std::make_unique<char[]>(packetSize);` |
| 锁 | `std::lock_guard` ×3 | `std::scoped_lock` ×3（C++17 限量） |
| 关闭套接字 | `closesocket` / `SD_BOTH` | `shutdown(m_sockClient, SHUT_RDWR); close(m_sockClient);`（客户端头文件已用 `sys/socket.h`，直接 `close`） |
| 错误打印 | `WSAGetLastError()` | `strerror(errno)` |
| 强转 | `(sockaddr*)` / `(char*)` | `reinterpret_cast` |
| `using namespace std` | `tcpnet.h` L9 | 删除，全文 `std::` 限定 |

### 4.2 接口层 `Tcpnet/INet.h` / `Kernel/IKernel.h`

- 两者已 `#include <sys/socket.h>`（Linux 风格），**include 无需改**。
- 客户端接口不含 `SOCKET` 参数，无需补 typedef。
- 仅把注释里的“Winsock / 初始化 Winsock”改为“socket / BSD socket”。

### 4.3 业务层 `Kernel/kernel.{h,cpp}`

| 维度 | 现状 | 改为 |
|------|------|------|
| 网络成员 | `INet* m_pNet;` | `std::unique_ptr<INet> m_pNet;` |
| 构造函数 | `m_pNet = new tcpnet(this);` | `m_pNet = std::make_unique<tcpnet>(this);` |
| 析构 | `delete m_pNet; m_pNet = NULL;` | `~Kernel() = default;`（整段删除） |
| `DealData` switch | 保留（首字节路由 + `sizeof` 校验不变） | 保留，仅 `reinterpret_cast` 换写法 |

### 4.4 UI 层 `mainwindow.{h,cpp}`

| 维度 | 现状 | 改为 |
|------|------|------|
| `uploadFileInfo` | `char szFilePath[FILE_PATH]; char szFileUploadTime[MAX_SIZE]; char szFileMD5[MAX_SIZE];` | `std::string filePath; std::string uploadTime; std::string md5;`（客户端内部结构，不进协议） |
| 上传任务列表 | `std::list<uploadFileInfo*> m_lstuploadFileInfo;` | `std::vector<std::unique_ptr<uploadFileInfo>> m_uploads;` |
| 内核成员 | `IKernel* m_pKernel;` | `std::unique_ptr<Kernel> m_kernel;`（`connect` 处 `(Kernel*)` 强转消失） |
| 登录/提取窗口 | `Login* m_pLogin; Dialog* m_pDialog;` | `std::unique_ptr<Login> m_login; std::unique_ptr<Dialog> m_dialog;` |
| 状态成员 | `long long Id; string filePath; int m_pos;` | `long long m_userId = 0; std::string m_downloadPath; std::streamoff m_downloadPos = 0;` |
| 响应槽强转 | `(const STRU_*_RS*)packet.constData()` ×9 | `reinterpret_cast<const STRU_*_RS*>(packet.constData())` |
| 任务查找 | `while` 遍历 + `strcmp` MD5 | `std::find_if(m_uploads.begin(), ..., [&](const auto& p){ return p->md5 == sur->szFileMD5; });` |
| 上传读文件 | `fopen` + `_fseeki64` + `fread` + `ferror` | `std::ifstream in{pInfo->filePath, std::ios::binary}; in.seekg(sur->m_pos, std::ios::beg); in.read(scr.m_FileContent, sizeof(...)); auto readNum = in.gcount();` 读尽判定 `in.eof()` |
| 下载写文件 | `fopen("r+b")` + `_fseeki64` + `fwrite` + `fclose` | `std::fstream file{m_downloadPath, std::ios::in|std::ios::out|std::ios::binary}; file.seekp(m_downloadPos, std::ios::beg); file.write(...); if (file) m_downloadPos += psds->m_fileNum;` |
| 下载建文件 | `fopen("wb")` + `fclose` | `{ std::ofstream out{m_downloadPath, std::ios::binary|std::ios::trunc}; if (!out) {...} }`（作用域结束自动关闭） |
| 表格取文件名 | `QString::section('/', -1)` | `QFileInfo(QString::fromStdString(pInfo->filePath)).fileName()` |
| `strcpy_s` ×12 | 组包 | `CopyToArray` ×12 |
| 新建任务 | `new uploadFileInfo` + `push_back`；失败 `remove`+`delete` | `auto info = std::make_unique<uploadFileInfo>(); ... m_uploads.push_back(std::move(info));` 失败 `m_uploads.pop_back();` |
| 释放任务 | `delete pInfo; erase(ite)` | `m_uploads.erase(it);` |

> ⚠️ 陷阱：`m_dialog` 现为 `new Dialog(this)`（有 Qt 父对象），改 `unique_ptr` 时构造参数必须传
> `nullptr`，否则 Qt 父窗口析构会二次 `delete`。`Login`/`Register` 无父对象，`unique_ptr` 安全。

### 4.5 `login` / `register` / `dialog`

- `login.h`：`Register* m_register;` → `std::unique_ptr<Register> m_register;`；析构只留 `delete ui;`。
- `login.cpp`：`m_register = std::make_unique<Register>(m_pKernel);`；`strcpy_s` ×2 → `CopyToArray` ×2。
- `register.cpp`：密码强度 `for` 循环 + ASCII 比较 → `std::any_of(PassWord.begin(), PassWord.end(), [](QChar c){ return c.isUpper(); })`（小写/数字同理）；`strcpy_s` ×2 → `CopyToArray`；强转 `reinterpret_cast`。
- `dialog.h`：删 `using namespace std;`；`string m_code;` → `std::string m_code;`。

### 4.6 `MD5/md5.{h,cpp}`

| 维度 | 现状 | 改为 |
|------|------|------|
| 定宽类型 | `typedef unsigned char byte; typedef unsigned long ulong;` | `using byte = std::uint8_t; using ulong = std::uint32_t;`（**正确性修复**：`ulong` 宽度随平台，必须定宽） |
| 成员数组 | `ulong _state[4]; ...` | `std::array<ulong,4> state_; std::array<ulong,2> count_; std::array<byte,64> buffer_; std::array<byte,16> digest_;` |
| 静态表 | `static const byte PADDING[64]; static const char HEX[16];` | `inline static constexpr std::array<byte,64> PADDING{0x80}; inline static constexpr char HEX[16] = {...};`（定义并入头，删 `.cpp` 定义） |
| 宏 | `S11..S44` / `UINT4` / `F/G/H/I` / `ROTATE_LEFT` / `FF/GG/HH/II` | `constexpr int`、`using UINT4 = std::uint32_t;`、`constexpr` 函数（`RotateLeft`/`F/G/H/I`/`StepF/G/H/I`） |
| 禁拷贝 | `MD5(const MD5&); MD5& operator=(const MD5&);` | `= delete` |
| 流参数 | `MD5(ifstream& in)` / `update(ifstream& in)` | `std::istream&`（更通用）；`update` 内删 `in.close()`（不应关闭调用者流） |
| `using std::ifstream/string` | 有 | 删除，全文 `std::` 限定 |
| 十六进制 | `t/16` / `t%16` | `HEX[t >> 4]` / `HEX[t & 0x0F]` |

> 验证：`MD5("abc") == 900150983cd24fb0d6963f7d28e17f72`，空串 `== d41d8cd98f00b204e9800998ecf8427e`。

---

## 5. 构建系统

### 5.1 客户端（保留 qmake，Linux 化）

- `Client/Tcpnet/Tcpnet.pri`：删除 `LIBS += -lws2_32`（Linux 的 socket 在 libc，无需链接）。
- `Client.pro` / 各 `.pri`：保持 `CONFIG += c++17`；可选加 `QMAKE_CXXFLAGS += -Wall -Wextra`。
- 构建：`cd Client && qmake6 Client.pro && make -j$(nproc)`（用系统 Qt6；`qmake6` 或 `/usr/lib/qt6/bin/qmake`）。
- 清理 Windows 产物：`Client/build`、`Client/release`、`Client/debug`、根目录 `ui_*.h`。

### 5.2 服务端（去 qmake，改 Makefile + CMake）

- 源文件 6 个：`main.cpp netWork/epoller.cpp netWork/session.cpp netWork/reactor.cpp CMySQL/cmysql.cpp Kernel/kernel.cpp`。
- 编译参数：`-std=c++17 -O2 -Wall -Wextra -pthread -D_FILE_OFFSET_BITS=64`（`-g` 用于 Debug）。
- MySQL：优先 `mysql_config --cflags --libs`；无此工具时写死 `-I/usr/include/mysql -lmysqlclient`
  （MySQL 8.4 可能还需 `-lz -lssl -lcrypto`，以 `mysql_config` 输出为准）。
- 产物：`build/Server`（Linux 无 `.exe`）；`clean`/`rebuild` 用 `rm -rf`/`mkdir -p`。
- Makefile 要点：显式源文件列表、`-MMD -MP` 自动头依赖、`.o` 输出到 `build/`。
- CMake 要点：`cmake_minimum_required(3.20)`、`add_executable`、`find_package(Threads REQUIRED)` +
  `Threads::Threads`、`target_compile_features(... cxx_std_17)`、`find_path`/`find_library` 定位 MySQL。
- 删除 `Server.pro`、3 个 `.pri`、qmake 生成物（`Makefile*`、`debug/`、`release/`、`build/`、`.qmake.stash`）。

---

## 6. 实施阶段（按序，每阶段结束可编译）

| 阶段 | 内容 | 验收 |
|------|------|------|
| **Phase 0** | 协议定长：两端 `packdef.h` 3 处 `long → int32_t` + 客户端调用点；`diff` 校验 | 两端 `diff` 为空；服务端/客户端各自编译通过 |
| **Phase 1** | 服务端去 Qt：`main.cpp` + `IKernel/INet` 去 winsock2 + `kernel` POSIX/`filesystem`/Meyers 单例/智能指针/`map` 上传表/string SQL + `CMySQL` 改造（3.2~3.8，网络层暂用 `std::thread` 替换 `CreateThread` 保 `select` 过渡，或直接上 Reactor） | 服务端 `make` 零告警；本机起 MySQL 后注册/登录/列表冒烟 |
| **Phase 2** | 服务端 Reactor：新增 `epoller/session/reactor`，重写 `tcpnet`，切换为单线程 epoll 事件循环（3.4~3.6） | 服务端全流程冒烟（含上传分块/下载分块/半包粘包） |
| **Phase 3** | 客户端 Linux 化 + 现代化：网络层 BSD socket + `std::thread` + 智能指针 + `scoped_lock`；`Kernel`/`mainwindow`/`login`/`register`/`dialog`/`MD5` 按第 4 节改造 | 客户端 `qmake && make` 零告警；连接本机服务端全功能回归 |
| **Phase 4** | 构建系统：服务端 Makefile + CMake，删 `.pro/.pri`；客户端清 Windows 产物；两端 `-Wall -Wextra` 零告警 | `make` 与 `cmake` 各编译一遍；`diff` 两端 `packdef.h` 为空 |
| **Phase 5** | 收尾：更新 `AGENTS.md`/`CLAUDE.md`/两端 `DOCS.md`（智能指针、无裸 `new`、单线程 Reactor、C++17 特性取舍）；删除三份旧规划文档的指引或标注已被 `UNIFIED_PLAN.md` 取代 | 文档与代码一致 |

> Phase 1 与 Phase 2 可合并为一步直接上 Reactor；拆开是为在“网络模型切换”与“业务去 Qt”之间
> 各留一个可编译的检查点。若对 Reactor 有信心，可合为一个大阶段。

---

## 7. 风险清单

| # | 风险 | 对策 |
|---|------|------|
| 1 | `long` 宽度变化破坏协议（最隐蔽） | Phase 0 先行，两端同步 `int32_t` 并 `diff` 校验 |
| 2 | 协议结构体误加 `std::string`/`vector` 成员破坏布局 | 红线 1 + `static_assert(is_trivially_copyable_v/sizeof)` 锁死 |
| 3 | ET 模式坑：必须循环读写到 `EAGAIN`，否则事件丢失连接卡死 | 代码注释 + 半包/粘包自测覆盖 |
| 4 | 半包/粘包：长度前缀状态机须严格处理 | `Session` 读缓冲累积 + 长度校验 `1..MAX_PACKET` |
| 5 | 大文件下载：响应块先入写缓冲靠 `EPOLLOUT` 刷出，不能在事件循环里阻塞等写 | Reactor 写缓冲设计；逐块回包 |
| 6 | 单线程阻塞：MySQL 慢查询/大块磁盘 IO 短暂拖慢所有连接 | 已知权衡（本地 + 4KB 分块毫秒级可接受），注释写明 |
| 7 | `std::thread` 未 `join` 即析构 → `std::terminate` | 两端析构均 `joinable()` 检查 + `join`；改线程生命周期必复检 |
| 8 | `Dialog` `unique_ptr` 与 Qt parent 双重释放 | parent 传 `nullptr`（4.4 陷阱） |
| 9 | MD5 `ulong` 改 `uint32_t` 手滑改 64 位算错 | 标准向量自测（`"abc"`/空串） |
| 10 | SQL 字符串拼接引号/百分号错乱 | 外部文本先 `EscapeString` 再拼；LIKE 的 `%` 属格式串写死 |
| 11 | `ofstream` 无返回字节数致续传进度失准 | 上传写入保留 `unique_ptr<FILE,&fclose>` + `fwrite` 精确字节数 |
| 12 | 数据库密码不应硬编码 | 经环境变量 `MYSQL_PASSWORD` 提供 |
| 13 | MySQL 头/库缺 `libmysqlclient-dev` | 阶段 0 先装；装不上则 Phase 1 用编译桩过渡 |

---

## 8. 回归验收清单（无自动化测试，手动）

1. 注册 / 重复注册 / 登录（对/错密码/不存在用户）
2. 文件列表（>15 条验证分页）
3. 上传：普通、秒传（同 MD5）、断点续传（中断后重传）、同名重复提示
4. 下载（与源文件 `md5sum` 比对）
5. 删除（引用计数 >1 时另一用户文件仍在）
6. 搜索（模糊匹配）
7. 分享生成提取码 / 他人提取 / 自己提取自己分享（应失败）
8. 服务端回车 / `Ctrl+C` 正常退出（验证 Reactor 线程 `join` 不挂死、`SIGINT` 优雅收尾）

---

## 附：相对三份旧文档的变化

- 取代 `MODERN_CPP_PLAN.md`：语言标准由“C++17 全面”收窄为“C++17 编译 + 主体 C++11/14 + 限量 C++17”，
  `std::scoped_lock`/`filesystem`/`string_view` 之外禁用 C++17 专属特性。
- 取代 `MIGRATION_PLAN.md`：服务端不再“保留 Qt + select 迁移”，改为去 Qt + epoll Reactor；
  客户端 Linux 化不再保留 Windows 条件编译分支（直接 BSD socket）。
- 取代 `Server/LINUX_REACTOR_PLAN.md`：语言标准由 C++14 提升到 C++17（允许少量 C++17）；
  文件系统操作由 POSIX（`mkdir`/`unlink`/`fseeko`）升级为 `std::filesystem`；服务端业务层同步叠加
  智能指针 / Meyers 单例 / `map` 上传表 / string SQL 现代化改造。
