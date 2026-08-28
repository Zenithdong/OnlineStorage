# Server Linux 化 + 单线程 Reactor 重构计划（无线程池版）

> 目标：服务器迁到 Linux 平台，完全脱离 Qt，网络层以 **单线程 Reactor（epoll）架构** 重写，
> 代码只用 C++11/14（编译标准 C++14），客户端（Windows Qt）与二进制协议保持不变。
> 业务（MySQL + 磁盘 IO）直接内联在 Reactor 事件循环中执行，**不使用线程池**。
>
> 现状（已实测）：g++ 15.2 / GNU Make 4.4 / CMake 4.2 齐备；MySQL 8.4 服务端已装、
> 缺开发库（libmysqlclient-dev）；两端 packdef.h 逐字一致。

## 1. 最高优先级风险：协议跨平台类型宽度（Phase 0 必修）

packdef.h 有 3 处 long 字段，Windows/MinGW x64 为 4 字节、Linux x86-64 为 8 字节，
直接部署必导致**上传、下载、搜索三个流程断链**：

| 结构体 | 字段 | 影响流程 |
|--------|------|----------|
| STRU_UPLOADFILECONTENT_RQ | long m_fileNum | 上传正文块有效长度错位 |
| STRU_DOWNLOADFILE_RS | long m_fileNum | 下载块有效长度错位 |
| STRU_SELECTFILE_RS | long szFileNum | 搜索页条数错位 |

修正方案：**两端同步**把上述 long 改为 int32_t（include cstdint）。
Windows 上 int32_t 与 long 同宽，客户端内存布局不变、零行为差异；
改完后用 diff 校验两端 packdef.h 逐字一致。
其余类型跨平台安全：char=1 / int=4 / long long=8，x86-64 两端对齐规则一致，两端皆为小端，
memcpy 直传可用。可选增强：两端各加 static_assert（sizeof/offsetof）做布局自检。

## 2. 目标架构：单线程 Reactor

    main 线程
      │ 初始化(存储目录/DB/监听) → 等待回车或 SIGINT 退出
      ▼
    Reactor 线程（epoll 事件循环，IO + 业务都在此线程，全程无锁）
      ├─ accept            → 新连接注册 EPOLLIN
      ├─ EPOLLIN           → 长度前缀解帧(4字节包长+包体)
      │                      完整帧 → 直接调用 kernel::dealData(fd, buf, len)
      │                      业务内执行：MySQL 查询 + 磁盘文件 IO + 响应入写缓冲
      ├─ EPOLLOUT          → 非阻塞写刷出写缓冲，写尽摘除 EPOLLOUT
      └─ EPOLLERR|EPOLLHUP → 关闭连接、清理 Session

**设计取舍（明确写出）**：
- 单线程下 MySQL 查询/磁盘写会短暂阻塞事件循环，慢查询会拖慢所有连接。
  教学项目规模 + 本地 MySQL + 4KB 分块 IO（毫秒级）完全可接受；这是拿并发性能换实现简洁。
- **换来的是无锁**：所有状态（连接表、收发缓冲、上传任务表 m_lstFileInfo、单 MYSQL 连接）
  只被 Reactor 线程访问，无需任何互斥锁。
  原 Windows 实现"每连接一线程 + 共享单 MYSQL 连接"本就存在线程安全隐患，单线程模型直接消除该隐患。
- **预留扩展点**：Reactor 中"完整帧 → 业务处理"收敛为一个 handler 调用点，
  未来若要恢复线程池，只需在这一处改为投递任务队列，其余代码不动。

## 3. 模块与文件规划

    Server/
    ├── main.cpp                     #【改】去 Qt；getchar 等回车退出 + SIGINT 优雅收尾
    ├── packdef.h                    #【改】3 处 long → int32_t（与 Client 同步）
    ├── Kernel/
    │   ├── IKernel.h                #【改】SOCKET → int，去 winsock2.h
    │   ├── kernel.h / kernel.cpp    #【改】fd 类型、POSIX 文件 API、Linux 存储路径（无需加锁）
    ├── netWork/
    │   ├── INet.h                   #【改】SOCKET → int，去 winsock2.h
    │   ├── epoller.h/.cpp           #【新】epoll RAII 封装（create1/add/mod/del/wait，约 60 行）
    │   ├── session.h/.cpp           #【新】每连接解帧状态机 + 读写缓冲
    │   └── reactor.h/.cpp           #【重写】原 tcpnet.* 替换为 epoll 事件循环 + 连接管理
    ├── CMySQL/
    │   └── cmysql.h/.cpp            #【改】mysql/mysql.h、qDebug→std::cerr（单线程，无需加锁）
    ├── Makefile                     #【新】Linux 手写版
    ├── CMakeLists.txt               #【新】Linux 版
    └── (删除) Server.pro、3 个 .pri、qmake 生成物、旧 release/、threadpool 不引入

### 3.1 Epoller
epoll_create1(EPOLL_CLOEXEC) 构造、析构 close；add/mod/del(fd, events, ptr)；
wait(events, maxevents, timeout)。**采用 ET 边缘触发**：读循环到 EAGAIN、写循环到 EAGAIN
（LT 作为降级备选写进注释）。
建议 epoll_wait 超时取 1000ms：停机标志用超时轮询检查即可，**不引入 eventfd**
（单线程无跨线程唤醒需求，少一个组件）。

### 3.2 Session（连接状态机）
    READ_HEADER(4B) → READ_BODY(len) → 投递完整帧 → 回到 READ_HEADER
- 读缓冲 vector<char> 累积半包，正确处理粘包/半包；长度前缀校验（1..MAX_PACKET，防脏数据）。
- 写缓冲 string + 已写偏移；发送先尝试非阻塞 write，写不完的余量入缓冲并挂 EPOLLOUT，
  EPOLLOUT 触发后继续刷，写尽摘除。
- 成员：fd、对端地址、状态机阶段、已读字节数；生命周期由 Reactor 的
  unordered_map<int, Session*> 管理（fd 变化时增删 epoll 事件）。

### 3.3 Reactor（替换原 TCPNet）
- 监听：socket(AF_INET) → SO_REUSEADDR → bind(0.0.0.0:8899) → listen(SOMAXCONN) → 非阻塞；
  沿用 INet 接口：InitNetWork / UnitNetWork / sendData / recvData。
- run() 主循环：epoll_wait → 分发 EPOLLIN（listen=accept / 连接=读帧并就地调 kernel::dealData）、
  EPOLLOUT（刷写缓冲）、EPOLLERR|EPOLLHUP（关闭连接）。
- 停机：atomic 标志 + 每次 wait 超时后检查；UnitNetWork 置标志 → join → 关闭全部连接。
- handler 调用点：完整帧处统一通过一个函数对象投递给 kernel::dealData，为将来线程池留口。

### 3.4 kernel 改造点（POSIX 化逐行清单）

| 原（Windows） | 新（Linux） |
|---------------|-------------|
| _fseeki64 / _ftelli64 | fseeko / ftello（加 -D_FILE_OFFSET_BITS=64 保险） |
| CreateDirectoryA + GetLastError | mkdir + errno==EEXIST 视为已存在 |
| DeleteFileA | unlink |
| GetLastError 报错输出 | errno + strerror(errno) |
| SOCKET socketWaiter（全部签名） | int socketWaiter |
| m_szSystemPath 硬编码 C:/_workspace/... | ./disk_file（相对运行目录） |
| 多线程共享单 MYSQL 连接（原隐患） | 单线程模型天然串行，无需改动 |

### 3.5 CMySQL 改造点
- 头文件：include <mysql/mysql.h>（Linux 安装路径，依赖 libmysqlclient-dev）。
- 4 处 qDebug → std::cerr（顺带完成去 Qt）。
- 单线程模型下连接只被 Reactor 线程使用，**不需要加锁**。

### 3.6 main.cpp
- 删 QCoreApplication；getchar() 等待回车退出；增加 SIGINT/SIGTERM 处理（signal + atomic 标志
  → kernel::close() 优雅收尾，可选加分项）。

## 4. C++ 标准与编码约束

- 编译标准 -std=c++14，**只用 C++11/14**：std::thread/mutex/condition_variable/atomic、
  std::function、unique_ptr/make_unique、lambda、enum class、range-for、std::random、
  chrono、unordered_map、std::array、=default/=delete、constexpr。
  （单线程版实际用不到 mutex/condvar，但标准仍限定 11/14。）
- 禁用 C++17 特性：string_view、optional、结构化绑定、if constexpr、CTAD、filesystem。
- 保持项目现有风格：原始指针管理网络层内存、结构体直接序列化、中文注释带"知识点"。

## 5. 实施阶段（按序）

- **Phase 0 协议定长化**：两端 packdef.h 3 处 long → int32_t；diff 校验逐字一致。
  不依赖 MySQL 库，现在即可做。
- **Phase 1 单线程 Reactor 重构**：重写网络层为 epoll + Session 解帧；kernel/CMySQL/main
  按 3.4~3.6 清单 POSIX 化；编译零告警；本机起 MySQL 后自测注册/登录/列表/上传/下载/
  搜索/分享全流程。
- **Phase 2 收尾**：Makefile + CMake 双构建版本、删除 .pro/.pri 与 qmake 生成物、
  更新 AGENTS/CLAUDE/DOCS 文档、有条件时与 Windows Qt 客户端联调。

## 6. 构建规划（Makefile + CMake，Linux 双版本）

共同点：
- 源文件 6 个：main.cpp netWork/epoller.cpp netWork/session.cpp netWork/reactor.cpp
  CMySQL/cmysql.cpp Kernel/kernel.cpp
- 编译参数：-std=c++14 -O2 -Wall -Wextra -pthread -D_FILE_OFFSET_BITS=64（-g 用于 Debug）
- MySQL：优先 mysql_config --cflags --libs；无此工具时写死 -I/usr/include/mysql -lmysqlclient
  （MySQL 8.4 可能还需 -lz -lssl -lcrypto，以 mysql_config 输出为准）
- 产物：build/Server（Linux 无 .exe）；清理命令用 rm -rf / mkdir -p

Makefile 要点：显式源文件列表、-MMD -MP 自动头依赖、输出 .o 到 build/、clean / rebuild 目标。
CMake 要点：cmake_minimum_required 3.20、add_executable、find_package(Threads REQUIRED) +
Threads::Threads、target_compile_features(cxx_std_14)、find_path/find_library 定位 MySQL 头与库。

## 7. 前置依赖与验证

- 本机已具备：g++ 15.2、GNU Make 4.4、CMake 4.2、MySQL 8.4 服务端（mysqld/mysql）。
- 待安装：sudo apt install libmysqlclient-dev（提供 mysql_config、/usr/include/mysql/mysql.h、
  libmysqlclient 库）——实施前先装，装不上则 Phase 1 用编译桩过渡。
- 数据库：执行 sql/init.sql 建库建表；本机 MySQL root 密码按实际设置（服务端经环境变量 MYSQL_PASSWORD 读取）
  硬编码连接参数（保持项目无配置文件风格）。
- 验证路径：make 与 cmake 各编译一遍零告警 → 启动 Server → 本机按协议自写测试客户端冒烟
  （或 nc 验 TCP 层）→ 有条件时用 Windows Qt 客户端远程联调全流程。

## 8. 风险清单（无线程池版）

1. 协议 long 宽度差异（已识别，Phase 0 解决）——不解决则上传/下载/搜索必挂。
2. 单线程阻塞是已知权衡：MySQL 慢查询/大块磁盘 IO 会短暂拖慢所有连接；
   本地部署 + 4KB 分块 + 毫秒级操作下可接受，注释中写明。
3. ET 模式坑：必须循环读写到 EAGAIN，否则事件丢失连接卡死——代码注释 + 自测覆盖。
4. 半包/粘包：长度前缀状态机须严格处理（Session 读缓冲累积）。
5. 大文件下载：原协议无结束标记、逐块回包；响应块先入写缓冲再靠 EPOLLOUT 刷出，
   绝不能在事件循环里阻塞等写。
6. 上传期间单线程被文件 IO 占用：4KB 块写入极快，且无并发写盘需求，风险低。
7. 客户端联调：客户端仅改 packdef 类型名，Windows 布局不变；两端 diff 校验防回归。
8. 未来并发提升路径已预留：handler 调用点单点改造即可接入线程池（到那时再加
   DB 串行化/连接池与 Session 写缓冲互斥）。
