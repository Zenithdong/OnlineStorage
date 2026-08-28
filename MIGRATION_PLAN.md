# Online_storage Windows → Linux 移植计划

> 目标:把当前仅支持 Windows(Qt 6.11 MinGW + Winsock2 + MySQL 8.4)的客户端/服务器,
> 在保持二进制协议、数据库和业务逻辑不变的前提下,移植到 Linux(x86-64)构建与运行。
> 原则:所有平台相关代码一律用条件编译改写,改动后 Windows 仍可编译。

---

## 1. 现状盘点(Windows 依赖清单)

### 1.1 协议层(改动必须两端同步,Client/packdef.h 与 Server/packdef.h 逐字一致)

| 问题 | 位置 | 说明 |
|------|------|------|
| long m_fileNum | STRU_UPLOADFILECONTENT_RQ | Windows(LLP64)long=4 字节;Linux x86-64(LP64)long=8 字节,sizeof 改变 -> 二进制协议破坏 |
| long szFileNum | STRU_SELECTFILE_RS | 同上 |
| long m_fileNum | STRU_DOWNLOADFILE_RS | 同上 |

其余结构体仅由 char 数组、long long、char 组成,两端 GCC 布局一致,无需改动。
长度前缀用本机 int32:Windows 与 Linux x86-64 均为小端,无需处理(ARM 另说,见第 3 节)。

### 1.2 网络层(改动最大)

**服务端 Server/netWork/tcpnet.{h,cpp}**

| Windows 用法 | Linux 替代 |
|--------------|-----------|
| include windows.h | sys/socket.h netinet/in.h arpa/inet.h unistd.h cerrno thread |
| WSAStartup/WSACleanup/WSAGetLastError | 删除(Linux 无需初始化;错误用 errno + strerror) |
| LOBYTE/HIBYTE 版本检查 | 删除 |
| SOCKET / INVALID_SOCKET / SOCKET_ERROR | 条件编译 typedef(见 2.2) |
| closesocket / SD_BOTH | close / SHUT_RDWR(条件宏) |
| SO_EXCLUSIVEADDRUSE | 删除,改 SO_REUSEADDR(便于重启) |
| SO_RCVTIMEO/SO_SNDTIMEO 参数 DWORD 毫秒 | struct timeval{5,0} |
| S_un.S_addr | sin_addr.s_addr |
| TIMEVAL / u_int | timeval / unsigned int |
| select(0,...) + fd_set.fd_count/fd_array 遍历 | select(nfds,...) (nfds=最大fd+1) + FD_ISSET 循环遍历 |
| FD_SETSIZE 容量判断(fd_count) | 判断新 fd >= FD_SETSIZE 时拒绝 |
| CreateThread / HANDLE 列表 / WaitForSingleObject / CloseHandle | std::thread / join() |
| static DWORD WINAPI ThreadSelect(LPVOID) | static void ThreadSelect(TCPNet*) |
| inet_ntoa | inet_ntop(线程安全) |

**客户端 Client/Tcpnet/tcpnet.{h,cpp}**

| Windows 用法 | Linux 替代 |
|--------------|-----------|
| WSAStartup/WSACleanup、LOBYTE/HIBYTE | 删除 |
| SOCKET/HANDLE/DWORD 成员 | 条件 typedef / std::thread |
| S_un.S_addr / inet_addr | sin_addr.s_addr / inet_pton |
| SO_SNDTIMEO(DWORD) | timeval |
| CreateThread / WaitForSingleObject / CloseHandle | std::thread / join() |
| closesocket / SD_BOTH | close / SHUT_RDWR |

注意:Client/Kernel/IKernel.h 与 Client/Tcpnet/INet.h 已经包含 sys/socket.h,
但 Linux 的 sys/socket.h 不定义 SOCKET,必须补平台 typedef(见 2.2)。

### 1.3 文件系统与 CRT(服务端 Server/Kernel/kernel.cpp + 客户端 UI)

| Windows 用法 | 出现处 | Linux 替代 |
|--------------|--------|-----------|
| CreateDirectoryA + GetLastError()==ERROR_ALREADY_EXISTS | kernel.cpp x3 | mkdir(path,0755) + errno==EEXIST |
| DeleteFileA | kernel.cpp x2 | remove() |
| GetFileAttributesA != INVALID_FILE_ATTRIBUTES | kernel.cpp x1 | access(path,F_OK) |
| _fseeki64 / _ftelli64 | kernel.cpp x1、mainwindow.cpp x2 | fseeko / ftello |
| _atoi64 | kernel.cpp x1 | atoll |
| strcpy_s | kernel.cpp x11、mainwindow.cpp x12、login.cpp x2、register.cpp x2 | snprintf(dst,sizeof(dst),"%s",src) |
| ZeroMemory | kernel.cpp x2 | memset |
| GetLastError | kernel.cpp x4 | errno |

### 1.4 构建系统与配置

| 问题 | 位置 |
|------|------|
| LIBS += -lws2_32(Linux 的 socket 在 libc 中,不需要) | Client/Tcpnet/Tcpnet.pri、Server/netWork/netWork.pri |
| MySQL 库硬编码 Windows 路径 libmysql.lib + include | Server/CMySQL/CMySQL.pri |
| 存储根路径硬编码 C:/_workspace/DevProjects/Online_storage/disk_file | Server/Kernel/kernel.cpp:12 |
| 构建命令 mingw32-make | 改 make |
| 数据库连接 root:<密码>@127.0.0.1:server | Server/Kernel/kernel.cpp open() |

---

## 2. 分阶段执行步骤

### 阶段 0:环境准备

    # Ubuntu 24.04+ 示例
    sudo apt install build-essential qt6-base-dev pkg-config libmysqlclient-dev
    # MySQL 8.x(库名 server)
    sudo apt install mysql-server        # 或 docker run -d -p 3306:3306 -e MYSQL_ROOT_PASSWORD=<你的密码> mysql:8.4
    sudo mysql -uroot -p < sql/init.sql

- Qt 用系统 Qt 6(qmake 路径 /usr/lib/qt6/bin/qmake 或 qmake6);项目仅用 core/widgets,C++17,兼容 Qt 6.4+。
- 若系统无 Qt6,可用官方在线安装器安装 Qt 6.8 LTS(仍带 qmake)。

### 阶段 1:修复协议类型(两端同步,先行做,避免中途协议不一致)

1. 两端 packdef.h 同步改三处(改后逐字一致):
   - STRU_UPLOADFILECONTENT_RQ::m_fileNum:long -> int
   - STRU_SELECTFILE_RS::szFileNum:long -> int
   - STRU_DOWNLOADFILE_RS::m_fileNum:long -> int
   - 同步更新注释(删除“Windows x64 下 long 为 32 位”等描述)。
2. 客户端 Client/mainwindow.cpp:scr.m_fileNum = (long)readNum; -> (int)readNum(readNum <= 4096,安全)。
3. 其余使用处(psds->m_fileNum、pssr->szFileNum、psus->m_fileNum、sds.m_fileNum)类型自动匹配,无需改。

### 阶段 2:公共 socket 兼容层

在 Server/netWork/INet.h、Server/Kernel/IKernel.h、Client/Tcpnet/INet.h、Client/Kernel/IKernel.h
的头部统一加入(替换现有 winsock2.h / sys/socket.h include):

    #ifdef _WIN32
    #  include <winsock2.h>
    #else
    #  include <sys/socket.h>
    #  include <netinet/in.h>
    #  include <arpa/inet.h>
    #  include <unistd.h>
    using SOCKET = int;
    constexpr SOCKET INVALID_SOCKET = -1;
    constexpr int SOCKET_ERROR = -1;
    #  define closesocket close
    #  define SD_BOTH SHUT_RDWR
    #endif

这样业务层 kernel.cpp / Kernel.h 中所有 SOCKET 参数零改动。

### 阶段 3:客户端网络层(Client/Tcpnet/tcpnet.{h,cpp})

1. 删除 WSAStartup/WSACleanup/m_bWsaStarted 及 LOBYTE/HIBYTE 检查;ConnectServer 直接 socket()。
2. m_sockClient 类型用兼容层 SOCKET;m_hThread 由 HANDLE 改 std::thread。
3. serverAddress.sin_addr.s_addr = inet_pton(AF_INET, szip, &serverAddress.sin_addr)(失败返回 false)。
4. SO_SNDTIMEO:timeval sendTimeout{5, 0}; + setsockopt 传 timeval。
5. ThreadRecv 改 static void ThreadRecv(tcpnet*);m_hThread = std::thread(&tcpnet::ThreadRecv, this);
   disConnectServer 中 WaitForSingleObject+CloseHandle -> m_hThread.join()。
6. CloseSocket:shutdown(m_sockClient, SHUT_RDWR); closesocket(m_sockClient);(宏已兼容)。
   Linux 下 shutdown 会让阻塞中的 recv 返回 0,线程安全退出——现有“清标志->关socket->join”顺序在 Linux 成立。
7. 错误打印 WSAGetLastError() -> strerror(errno)。

### 阶段 4:服务端网络层(Server/netWork/tcpnet.{h,cpp})

1. 头文件:std::list<HANDLE> m_lstHandle -> std::thread m_thread;DWORD WINAPI ThreadSelect(LPVOID) -> static void ThreadSelect(TCPNet*)。
2. InitNetWork:
   - 删除 WSAStartup/版本检查/m_bWsaStarted 相关(复用 m_sockListen != INVALID_SOCKET 判断已启动)。
   - SO_EXCLUSIVEADDRUSE 段替换为 SO_REUSEADDR。
   - serverAddress.sin_addr.s_addr = dwip;(htons 保留)。
   - CreateThread -> m_thread = std::thread(&TCPNet::ThreadSelect, this);
3. UnitNetWork 退出顺序调整(Linux 上另一线程 close() 一个正被 select 监控的 fd 是未定义行为):
   - 保持:m_bQuitFlag=false -> 对全部客户端 shutdown(SHUT_RDWR);
   - 把 closesocket(m_sockListen) 与客户端 closesocket 移到 m_thread.join() 之后;
   - 线程靠现有 100ms select 超时轮询 m_bQuitFlag 退出(最坏延迟约 100ms,可接受)。
4. 接收超时:DWORD timeoutMs -> timeval timeout{5, 0};两个 setsockopt 改传 timeval。
5. ThreadSelect:
   - 锁内复制集合时同时计算 nfds = max(fd)+1;
   - select(nfds, &readableSockets, nullptr, nullptr, &timeout);
   - 遍历改为 for (int fd = 0; fd < FD_SETSIZE; ++fd) { if (!FD_ISSET(fd, &readableSockets)) continue; ... },
     fd == listenSocket 时 accept(accept 返回 int),否则按原逻辑收包;
   - 容量判断:新 fd >= FD_SETSIZE 时 close 并拒绝;
   - inet_ntoa -> inet_ntop(char ip[INET_ADDRSTRLEN]);
   - u_int -> unsigned int;TIMEVAL -> timeval。
6. 删除 recvData() 空实现或保留(接口兼容)。

### 阶段 5:服务端业务层(Server/Kernel/kernel.cpp)

1. 存储路径:strcpy(m_szSystemPath, "C:/_workspace/DevProjects/Online_storage/disk_file");
   -> Linux 绝对路径(如 "/home/zenith/workspace/Online_storage/disk_file")。路径拼接已用 /,跨平台可用。
2. open() 目录创建:if (mkdir(m_szSystemPath, 0755) != 0 && errno != EEXIST) {...}
   RegisterRq 与 UploadFileLisRq 中两处 CreateDirectoryA 同理(mkdir(userDirectory,0755),errno==EEXIST 视为成功)。
3. DeleteFileA(path) -> remove(path);GetFileAttributesA(filePath) != INVALID_FILE_ATTRIBUTES -> access(filePath, F_OK) == 0。
4. GetDiskFileSize:_fseeki64/_ftelli64 -> fseeko/ftello(x86-64 默认 64 位偏移)。
5. _atoi64 -> atoll(LoginRq)。
6. 全部 11 处 strcpy_s -> snprintf(dst, sizeof(dst), "%s", src)。
7. ZeroMemory(x, sizeof(x)) -> memset(&x, 0, sizeof(x))。
8. 删除 windows.h 依赖(GetLastError -> errno);保留 IsSafeFileName 现有名单(含反斜杠与 Windows 保留字符)
   以保证与 Windows 客户端协议兼容;如只连 Linux 客户端可放宽为仅拒绝 / 与 NUL。
9. 删除 windows.h 后若报 min/max 宏缺失——本项目未使用(grep 确认 "min" 命中均为 HasTerminator),无需处理。

### 阶段 6:客户端 UI 层

1. Client/mainwindow.cpp:12 处 strcpy_s -> snprintf(...);2 处 _fseeki64 -> fseeko。
2. Client/login.cpp、Client/register.cpp:各 2 处 strcpy_s -> snprintf(...)。
3. QString::section('/', -1) 取文件名在 Linux 上成立(QFileDialog 两端都返回 / 分隔路径),无需改;
   可选优化为 QFileInfo(filePath).fileName()。
4. 其余均为纯 Qt API,跨平台。

### 阶段 7:构建系统(.pro/.pri)

1. Client/Tcpnet/Tcpnet.pri 与 Server/netWork/netWork.pri:LIBS += -lws2_32 -> win32: LIBS += -lws2_32。
2. Server/CMySQL/CMySQL.pri:

    win32 {
        LIBS += "C:\\Program Files\\MySQL\\MySQL Server 8.4\\lib\\libmysql.lib"
        INCLUDEPATH += "C:\\Program Files\\MySQL\\MySQL Server 8.4\\include"
    }
    unix {
        CONFIG += link_pkgconfig
        PKGCONFIG += mysqlclient
    }

   (等价写法:unix: LIBS += -lmysqlclient + unix: INCLUDEPATH += /usr/include/mysql。)
3. 删除/忽略 Client/build、Client/release、根目录 Client/ui_*.h 等 Windows 构建产物。

### 阶段 8:编译与回归验证

    cd Server && /usr/lib/qt6/bin/qmake Server.pro && make -j$(nproc)
    cd ../Client && /usr/lib/qt6/bin/qmake Client.pro && make -j$(nproc)
    # 先启动 MySQL 并导入 sql/init.sql,再运行服务端
    ./Server/server &
    ./Client/client   # 连接 127.0.0.1:8899

手动回归清单(无自动化测试):

1. 注册 / 重复注册 / 登录(对/错密码/不存在用户)
2. 文件列表(>15 条时验证分页)
3. 上传:普通上传、秒传(同 MD5)、断点续传(中断后重传)、同名重复提示
4. 下载(与源文件 md5sum 对比)
5. 删除(引用计数 >1 时另一用户文件仍在)
6. 搜索(模糊匹配)
7. 分享生成提取码 / 他人提取 / 自己提取自己分享(应失败)
8. 服务端回车正常退出(验证 select 线程 join 不挂死)

---

## 3. 关键风险与对策

| # | 风险 | 对策 |
|---|------|------|
| 1 | long 宽度变化破坏协议(最隐蔽) | 阶段 1 先行,两端 packdef.h 同步改 int 并保证逐字一致 |
| 2 | Linux 上 close() 正被 select 监控的 fd 是 UB,可能导致退出挂死 | UnitNetWork 调整顺序:先 shutdown、join(靠 100ms 超时轮询退出)、后 close |
| 3 | SO_RCVTIMEO/SO_SNDTIMEO 参数语义不同(DWORD 毫秒 vs timeval) | 统一 timeval{5,0} |
| 4 | fd_set 结构不同(fd_count/fd_array 是 Windows 专属) | FD_ISSET 循环遍历;容量判断改用 fd 值 |
| 5 | strcpy_s/_atoi64/_fseeki64/_ftelli64/ZeroMemory 是 MSVC/MinGW CRT 扩展,glibc 无 | 全部替换为标准 C/POSIX 函数 |
| 6 | MySQL 库路径硬编码 | pkg-config 化 |
| 7 | 存储根路径硬编码 Windows 盘符 | 改为 Linux 路径 |
| 8 | 字节序/对齐 | 两端同为 x86-64 小端 + GCC,一致;跨 ARM 平台时需重做序列化(本次不做) |

---

## 4. 可选优化(非必须,建议后续)

1. select -> poll():消除 FD_SETSIZE(1024)并发上限,退出更干净。
2. SOCKET 兼容层抽成独立头文件(如 common/socket_compat.h)供两端 include,减少重复。
3. 硬编码配置(IP/端口/DB 账号/存储路径)抽到配置文件。
4. 清理 Client/build、Client/release 等生成物并补充 .gitignore。
5. 协议加包尾/CRC 与显式序列化(为 ARM/跨架构做准备)。

## 5. 文件改动汇总

| 文件 | 改动 |
|------|------|
| Client/packdef.h、Server/packdef.h | 3 处 long -> int(两端同步) |
| Client/Tcpnet/tcpnet.{h,cpp} | Winsock -> BSD socket,CreateThread -> std::thread |
| Client/Tcpnet/INet.h、Client/Kernel/IKernel.h | 加跨平台 SOCKET typedef 块 |
| Server/netWork/tcpnet.{h,cpp} | Winsock select -> BSD select,线程改造,退出顺序调整 |
| Server/netWork/INet.h、Server/Kernel/IKernel.h | winsock2.h -> 跨平台 typedef 块 |
| Server/Kernel/kernel.cpp | 文件 API/CRT 函数替换、存储路径 |
| Client/mainwindow.cpp、login.cpp、register.cpp | strcpy_s/_fseeki64 替换 |
| Client/Tcpnet/Tcpnet.pri、Server/netWork/netWork.pri | win32: LIBS += -lws2_32 |
| Server/CMySQL/CMySQL.pri | unix 分支 pkg-config mysqlclient |
| sql/init.sql、两端 Kernel/、MD5/、CMySQL/cmysql.* | 无需改动(纯标准/跨平台) |
