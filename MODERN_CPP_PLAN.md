# Online_storage 现代 C++ 改造计划(C++17 版,按目录分类)

> 说明:本版不使用任何 C++20 特性;工程保持 CONFIG += c++17 不变。C++20 计划中的三处特性
> 已按下表替换,其余条目与原计划一致。

## 0. 标准与红线

### 0.1 C++20 -> C++17 的替换对照

| 原计划(C++20) | 本版(C++17) | 影响位置 |
|---------------|------------|---------|
| std::format 拼 SQL/路径 | std::string + 运算符拼接 + std::to_string | Server/Kernel/kernel.cpp 全部 SQL 与路径 |
| std::jthread(自动 join、request_stop) | std::thread(手动 joinable()+join) | 两端 tcpnet.{h,cpp} |
| std::span | 不用(普通 for 循环) | 随机码生成、接口签名不改 |

工程文件 Client/Client.pro 与 Server/Server.pro 保持 CONFIG += c++17,无需改动
(可选加 QMAKE_CXXFLAGS += -Wall -Wextra,逐步消警告)。

### 0.2 三大红线(每一步都必须遵守)

1. **协议布局不变**:packdef.h 结构体只含 char 数组与基础整数,**禁止引入 std::string / std::vector /**
   **std::array 等类类型成员**;唯一允许的成员类型改动是 3 个 long -> int32_t。
2. **两端 packdef.h 逐字一致**:改完一端复制到另一端,diff 必须为空。
3. **每阶段保持可编译可运行**:先机械替换、后结构改造;每阶段编译一次
   (cd Server && qmake Server.pro && mingw32-make;Client 同理)。

## 1. 全局通用规则(适用于所有文件的机械替换)

- **NULL -> nullptr**:Client/Kernel/kernel.cpp、Server/Kernel/kernel.cpp 析构中(多数随智能指针改造直接删除)。
- **C 风格强转 -> reinterpret_cast**(协议结构体与字节互转)/ **static_cast**(数值):全部文件。
- **删除 using namespace std**:Client/Tcpnet/tcpnet.h、Server/CMySQL/cmysql.h、Client/dialog.h;
  Client/MD5/md5.h 的 using std::ifstream / using std::string 也删除,全文 std:: 限定。
- **std::lock_guard -> std::scoped_lock**:Server/netWork/tcpnet.cpp 4 处、Client/Tcpnet/tcpnet.cpp 3 处。
- **(void) 形参消音 -> 无名形参**:Server/Kernel/kernel.cpp 的 UploadFileContentRq、DeleteFileRq 开头。
- **CopyToArray 模板替代 strcpy_s**(各用到的 .cpp 匿名 namespace 内):

    namespace {
    template <size_t N>
    void CopyToArray(char (&dst)[N], const char* src) {
        std::strncpy(dst, src, N - 1);
        dst[N - 1] = '\0';
    }
    }

- **ZeroMemory -> 元素重置**:只清数组不清 m_nType:for (auto& info : arr) info = {};
  禁止写 xxx = {}; 整体赋值(会清零 m_nType)。
- **srand/rand -> std::mt19937 + std::uniform_int_distribution**(Server/Kernel/kernel.cpp ShareLinkRq)。
- **atoll/atoi/_atoi64 -> std::stoll / std::stoi / std::from_chars**(Server/Kernel/kernel.cpp 多处)。

---

## 2. 共享协议头(Client/packdef.h 与 Server/packdef.h,两份必须逐字一致)

改法相同,一次改完直接复制到另一端再 diff。

| 位置 | 现状 | 改为 |
|------|------|------|
| 头部宏区(协议号 22 个) | #define _default_protocol_register_rq _default_protocol_base + 1 | inline constexpr char _default_protocol_register_rq = _default_protocol_base + 1;(_default_protocol_base 及全部协议号同理) |
| 尺寸宏 5 个 | #define MAX_SIZE 50 等 | inline constexpr int MAX_SIZE = 50; 等 |
| 结果码宏 | #define _register_res_failed 0 等 | inline constexpr char _register_res_failed = 0; 等 |
| STRU_UPLOADFILECONTENT_RQ | long m_fileNum = 0; | int32_t m_fileNum = 0;(#include <cstdint>) |
| STRU_SELECTFILE_RS | long szFileNum = 0; | int32_t szFileNum = 0; |
| STRU_DOWNLOADFILE_RS | long m_fileNum = 0; | int32_t m_fileNum = 0; |
| 每个结构体后(新增) | — | static_assert(std::is_trivially_copyable_v<T>); static_assert(sizeof(T) == 当前实际值);(#include <type_traits>;禁止断言 is_standard_layout,成员分布在基类+派生类会失败) |
| STRU_BASE(可选) | char m_nType = _default_protocol_base; | 加 STRU_BASE() = default; 与 explicit STRU_BASE(char type);派生类构造改初始化列表 |

调用点同步:Client/mainwindow.cpp 上传循环 scr.m_fileNum = (long)readNum; -> = static_cast<int32_t>(readNum);

---

## 3. Server/ 目录

### 3.1 Server/main.cpp

| 位置 | 现状 | 改为 |
|------|------|------|
| L12/L30/L36 | kernel::GetKernel()->open() / ->close() | kernel::GetKernel().open() / .close()(共 4 处,配合 Meyers 单例) |
| getchar/EOF 逻辑 | 保留 | 保留 |

### 3.2 Server/Kernel/kernel.h

| 位置 | 现状 | 改为 |
|------|------|------|
| 头文件区 | — | 增加 #include <memory> <string> <map> |
| fileinfo 结构体 | FILE* pFile; | std::unique_ptr<FILE, decltype(&fclose)> pFile;(其余成员加 = 0 默认初始化;推荐此方案保留 fwrite 精确进度语义) |
| 单例声明 | static kernel* m_pKernel; | 删除声明;GetKernel() 返回类型 kernel* -> kernel& |
| 资源成员 | INet* m_pNet; CMySql* m_pSql; | std::unique_ptr<INet> m_pNet; std::unique_ptr<CMySql> m_pSql; |
| 存储路径成员 | char m_szSystemPath[FILE_PATH]; | std::string m_systemPath; |
| 上传任务表 | list<fileinfo*> m_lstFileInfo; | std::map<std::pair<long long, long long>, std::unique_ptr<fileinfo>> m_uploads;(pair 自带 operator<) |
| 析构函数 | ~kernel();(手工 delete) | ~kernel() = default; |

### 3.3 Server/Kernel/kernel.cpp

| 位置 | 现状 | 改为 |
|------|------|------|
| L18 单例定义 | kernel* kernel::m_pKernel = new kernel; | 删除;GetKernel 改 kernel& kernel::GetKernel() { static kernel instance; return instance; } |
| 构造函数 | m_pNet = new TCPNet; m_pSql = new CMySql; strcpy(m_szSystemPath, "C:/...") | m_pNet = std::make_unique<TCPNet>(); m_pSql = std::make_unique<CMySql>(); m_systemPath = "C:/_workspace/DevProjects/Online_storage/disk_file"; |
| 析构函数 | delete m_pNet/m_pSql 整段 | 整段删除(= default) |
| open() 建目录 | CreateDirectoryA + GetLastError() == ERROR_ALREADY_EXISTS | std::error_code ec; std::filesystem::create_directory(m_systemPath, ec); if (ec && ec != std::errc::file_exists) |
| GetDiskFileSize() | fopen + _fseeki64 + _ftelli64 | std::filesystem::file_size(path, ec); ec 时返回 -1(可整体删除) |
| dealData() 10 个 case | (const STRU_*_RQ*)szbuf | reinterpret_cast<const STRU_*_RQ*>(szbuf) |
| 各 Rq() 函数 | (STRU_*_RQ*)szbuf / (char*)&rs | reinterpret_cast |
| 全部 SQL 构造(9 个函数) | char szsql[SQLLEN] + snprintf | std::string sql = "select ... where u_name = '" + userName + "'";数值用 std::to_string;LIKE 处保留 % 号拼入 |
| 全部路径构造 | snprintf(FilePath, sizeof, "%s/%llu", ...) | std::string dir = m_systemPath + "/" + std::to_string(userId); |
| RegisterRq 用户目录 | CreateDirectoryA(FilePath, ...) | std::filesystem::create_directory(userDirectory, ec) + ec 判断 |
| LoginRq 解析 ID | _atoi64(userId.c_str()) | std::stoll(userId) |
| GetFileLisRq/SelectFileRq | list<string> 弹栈 + strcpy_s + ZeroMemory + atoll | CMySQL 新接口 rows 索引(见 3.6)+ CopyToArray + for (auto& info : arr) info = {}; + std::stoll |
| UploadFileLisRq | CreateDirectoryA / GetFileAttributesA / DeleteFileA / fopen("ab"/"wb") / new fileinfo | filesystem::create_directory / exists / remove / pFile.reset(fopen(path, "ab")) / make_unique<fileinfo> + m_uploads.emplace(key, move) |
| UploadFileLisRq 续传查找 | for (fileinfo* info : m_lstFileInfo) 线性查 | auto it = m_uploads.find({userId, fileId}); |
| UploadFileContentRq | (void)socketWaiter; while 线性查找;fwrite;erase(ite) | 无名形参;m_uploads.find({userid, fileid});fwrite(p->pFile.get(), ...);完成后 m_uploads.erase(it) |
| DeleteFileRq | (void)socketWaiter; DeleteFileA; atoll/atoi | 无名形参;filesystem::remove;std::stoll/stoi |
| ShareLinkRq | srand(time(NULL)) + rand()%36 | std::mt19937 engine{std::random_device{}()}; std::uniform_int_distribution<int> dist(0, 35); constexpr char TABLE[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"; for (int i = 0; i < 4; ++i) szcode[i] = TABLE[dist(engine)]; |
| DownLoadFileRq | FILE* pFile = fopen(path, "rb") + fread + fclose | std::ifstream in{filePath, std::ios::binary}; while (in.read(sds.m_FileContent, ONE_PAGE), (num = (int)in.gcount()) > 0) |
| close() | for 循环 fclose + delete | m_uploads.clear();(unique_ptr + FILE deleter 自动释放) |

### 3.4 Server/Kernel/IKernel.h

| 位置 | 现状 | 改为 |
|------|------|------|
| L3 | #include <winsock2.h> | 跨平台块:#ifdef _WIN32 winsock2.h;#else sys/socket.h 等 + using SOCKET = int; constexpr SOCKET INVALID_SOCKET = -1; constexpr int SOCKET_ERROR = -1; #define closesocket close #define SD_BOTH SHUT_RDWR #endif(与迁移计划 2.2 一致) |

### 3.5 Server/CMySQL/cmysql.h

| 位置 | 现状 | 改为 |
|------|------|------|
| L6 | using namespace std; | 删除 |
| 句柄成员 | MYSQL* mysql; | std::unique_ptr<MYSQL, decltype(&mysql_close)> mysql;(#include <memory>) |
| SelectMysql 声明 | bool SelectMysql(const char* sql, int nColumn, list<string>& lst); | std::vector<std::vector<std::string>> SelectMysql(const std::string& sql, int nColumn);(失败返回空 vector;#include <vector> <string>) |
| UpdateMysql 声明 | bool UpdateMysql(const char* sql); | bool UpdateMysql(const std::string& sql); |
| EscapeString 声明 | string EscapeString(const char* value) const; | std::string EscapeString(std::string_view value) const;(#include <string_view>) |

### 3.6 Server/CMySQL/cmysql.cpp

| 位置 | 现状 | 改为 |
|------|------|------|
| 构造函数 | mysql(mysql_init(nullptr)) | mysql_(mysql_init(nullptr), &mysql_close) |
| DisConnect() | mysql_close(mysql); mysql = nullptr; | mysql_.reset(); |
| ConnectMySql 重连 | if (!mysql) { mysql = mysql_init(nullptr); ... } | if (!mysql_) { mysql_.reset(mysql_init(nullptr)); ... } |
| 全部句柄访问 | mysql-> / mysql | mysql_.get()-> |
| SelectMysql 结果集 | list<string>& 扁平追加 | rows.push_back(vector<string>) 每行 nColumn 个;mysql_fetch_row 循环填充;free_result 保留 |
| UpdateMysql | const char* sql | const std::string& sql,内部 sql.c_str() |
| EscapeString | const char* value | std::string_view value;strlen 改 value.size() |

### 3.7 Server/netWork/INet.h

| 位置 | 现状 | 改为 |
|------|------|------|
| L3 | #include <winsock2.h> | 与 3.4 相同的跨平台块 |

### 3.8 Server/netWork/tcpnet.h

| 位置 | 现状 | 改为 |
|------|------|------|
| 头文件区 | <atomic> <cstdio> <list> <mutex> | <list> 改 <vector>;加 <thread> |
| 线程句柄 | std::list<HANDLE> m_lstHandle; | std::thread m_thread; |
| 线程入口 | static DWORD WINAPI ThreadSelect(LPVOID lp); | static void ThreadSelect(TCPNet* self); |
| 客户端集合 | std::list<SOCKET> m_lstSocket; | std::vector<SOCKET> m_sockets; |
| 命名(可选) | m_bQuitFlag / m_bWsaStarted | m_quitFlag / m_wsaStarted |

### 3.9 Server/netWork/tcpnet.cpp

| 位置 | 现状 | 改为 |
|------|------|------|
| InitNetWork 线程创建 | HANDLE thread = CreateThread(nullptr, 0, &ThreadSelect, this, 0, nullptr); m_lstHandle.push_back(thread); | m_thread = std::thread(&TCPNet::ThreadSelect, this); |
| UnitNetWork 等待线程 | for (HANDLE thread : m_lstHandle) { WaitForSingleObject(...); CloseHandle(...); } | if (m_thread.joinable()) m_thread.join();(保持先清标志、shutdown 客户端唤醒 select 的顺序) |
| ThreadSelect 开头 | TCPNet* network = (TCPNet*)lp; | 参数直接 TCPNet* self |
| 收包缓冲 | char* packet = new char[packetSize]; ... delete[] packet; | auto packet = std::make_unique<char[]>(packetSize); |
| 类型 | u_int index | unsigned int index |
| 锁 | std::lock_guard x4 | std::scoped_lock x4 |
| 强转 | (LPVOID)、(char*) | reinterpret_cast |
| select/fd_set 结构 | Windows 形式保留 | 保留(迁移 Linux 时按 MIGRATION_PLAN 阶段 4 改) |

注意:std::thread 析构前必须 join,否则 std::terminate;析构函数已调用 UnitNetWork(),满足要求,
但每次改动线程生命周期都要复检这条。

---

## 4. Client/ 目录

### 4.1 Client/main.cpp

无改动。

### 4.2 Client/Kernel/IKernel.h

| 位置 | 现状 | 改为 |
|------|------|------|
| L3 | #include <sys/socket.h> | 与 3.4 相同的跨平台块(Windows 分支 winsock2.h) |

### 4.3 Client/Kernel/kernel.h

| 位置 | 现状 | 改为 |
|------|------|------|
| 头文件区 | — | 加 #include <memory> |
| 网络成员 | INet* m_pNet; | std::unique_ptr<INet> m_pNet; |
| 析构 | ~Kernel(); | ~Kernel() = default;(或删除声明) |

### 4.4 Client/Kernel/kernel.cpp

| 位置 | 现状 | 改为 |
|------|------|------|
| 构造函数 | m_pNet = new tcpnet(this); | m_pNet = std::make_unique<tcpnet>(this); |
| 析构函数 | delete m_pNet; m_pNet = NULL; | 整段删除 |
| DealData() switch | 保留(首字节路由 + sizeof 校验不变) | 保留 |

### 4.5 Client/Tcpnet/INet.h

| 位置 | 现状 | 改为 |
|------|------|------|
| L3 | #include <sys/socket.h> | 与 3.4 相同的跨平台块 |

### 4.6 Client/Tcpnet/tcpnet.h

| 位置 | 现状 | 改为 |
|------|------|------|
| L9 | using namespace std; | 删除 |
| 头文件区 | — | 加 <thread> |
| 线程句柄 | HANDLE m_hThread; | std::thread m_thread; |
| 线程入口 | static DWORD WINAPI ThreadRecv(LPVOID lp); | static void ThreadRecv(tcpnet* self); |

### 4.7 Client/Tcpnet/tcpnet.cpp

| 位置 | 现状 | 改为 |
|------|------|------|
| ConnectServer 线程创建 | m_hThread = CreateThread(nullptr, 0, &ThreadRecv, this, 0, nullptr); | m_thread = std::thread(&tcpnet::ThreadRecv, this); |
| disConnectServer 等待线程 | WaitForSingleObject(m_hThread, INFINITE); CloseHandle(m_hThread); m_hThread = nullptr; | if (m_thread.joinable()) m_thread.join();(保持先清标志、CloseSocket shutdown 唤醒 recv 的顺序) |
| ThreadRecv 开头 | tcpnet* network = (tcpnet*)lp; | 参数直接 tcpnet* self |
| RecvData 收包 | char* packet = new char[packetSize]; 两处 delete[] | auto packet = std::make_unique<char[]>(packetSize); |
| 锁 | std::lock_guard x3 | std::scoped_lock x3 |
| 强转 | (sockaddr*)、(char*) | reinterpret_cast |
| 错误输出 | printf / WSAGetLastError | 保留 printf 或改 std::cerr;错误码部分按迁移计划改 strerror(errno) |

### 4.8 Client/mainwindow.h

| 位置 | 现状 | 改为 |
|------|------|------|
| 头文件区 | — | 加 <memory> <vector> <string> |
| uploadFileInfo 结构体 | char szFilePath[FILE_PATH]; char szFileUploadTime[MAX_SIZE]; char szFileMD5[MAX_SIZE]; | std::string filePath; std::string uploadTime; std::string md5;(客户端内部结构,不进协议) |
| 结构体其余成员 | long long m_szFileSize; long long m_pos; | long long fileSize = 0; long long pos = 0; |
| 上传任务列表 | std::list<uploadFileInfo*> m_lstuploadFileInfo; | std::vector<std::unique_ptr<uploadFileInfo>> m_uploads; |
| 内核成员 | IKernel* m_pKernel; | std::unique_ptr<Kernel> m_kernel;(connect 处 (Kernel*) 强转消失) |
| 登录/提取窗口 | Login* m_pLogin; Dialog* m_pDialog; | std::unique_ptr<Login> m_login; std::unique_ptr<Dialog> m_dialog; |
| 状态成员命名 | long long Id; string filePath; int m_pos; | long long m_userId = 0; std::string m_downloadPath; std::streamoff m_downloadPos = 0; |
| FileDigest | string FileDigest(const string& filr); | std::string FileDigest(const std::string& file); |

陷阱:m_dialog 现为 new Dialog(this)(有父对象),改 unique_ptr 时构造参数必须改 nullptr,
否则 Qt 父窗口析构会二次 delete。Login/Register 无父对象,unique_ptr 安全。

### 4.9 Client/mainwindow.cpp

| 位置 | 现状 | 改为 |
|------|------|------|
| 构造函数 | m_pKernel = new Kernel; m_pLogin = new Login(m_pKernel); | m_kernel = std::make_unique<Kernel>(); m_login = std::make_unique<Login>(m_kernel.get()); 后续 m_pKernel-> 改 m_kernel-> |
| 构造函数 8 条 connect | connect((Kernel*)m_pKernel, &Kernel::LoginRs, ...) | connect(m_kernel.get(), &Kernel::LoginRs, ...) |
| 析构函数 | DisConnect + delete m_pLogin + delete m_pKernel + for delete 上传列表 | m_kernel->DisConnect(); 其余删除(unique_ptr 自动清理) |
| 9 个响应槽 | (const STRU_*_RS*)packet.constData() | reinterpret_cast<const STRU_*_RS*>(packet.constData()) |
| UploadFileInfoRS 任务查找 | while 遍历 + strcmp MD5 | auto it = std::find_if(m_uploads.begin(), m_uploads.end(), [&](const auto& p) { return p->md5 == sur->szFileMD5; }); |
| UploadFileInfoRS 读文件 | FILE* pFile = fopen(pInfo->szFilePath, "rb"); _fseeki64(pFile, sur->m_pos, SEEK_SET) | std::ifstream in{pInfo->filePath, std::ios::binary}; if (!in) {...} in.seekg(sur->m_pos, std::ios::beg); |
| 上传发送循环 | fread + (long)readNum + ferror | in.read(scr.m_FileContent, sizeof(...)); auto readNum = in.gcount(); scr.m_fileNum = static_cast<int32_t>(readNum); 读尽判定改 contentSent = in.eof(); |
| 表格追加文件名 | QString::fromStdString(string(pInfo->szFilePath)).section('/', -1) | QFileInfo(QString::fromStdString(pInfo->filePath)).fileName() |
| 任务释放 | delete pInfo; erase(ite) | m_uploads.erase(it); |
| DownLoadFileRs 写文件 | FILE* pFile = fopen(filePath.c_str(), "r+b"); _fseeki64(pFile, m_pos, SEEK_SET); fwrite; fclose | std::fstream file{m_downloadPath, std::ios::in | std::ios::out | std::ios::binary}; file.seekp(m_downloadPos, std::ios::beg); file.write(...); if (file) m_downloadPos += psds->m_fileNum; |
| 上传组包 strcpy_s x6(L324-335) | strcpy_s | CopyToArray x6 |
| 新建上传任务 | new uploadFileInfo + push_back;失败 remove+delete | auto info = std::make_unique<uploadFileInfo>(); ... m_uploads.push_back(std::move(info)); 失败 m_uploads.pop_back(); |
| 搜索/删除/分享/提取/下载 5 个槽 | strcpy_s x6(L354、376、395、417-418、457) | CopyToArray x6 |
| 提取对话框 | delete m_pDialog; m_pDialog = new Dialog(this); | m_dialog = std::make_unique<Dialog>(nullptr); |
| 下载建文件 | FILE* targetFile = fopen(filePath.c_str(), "wb"); fclose | { std::ofstream out{m_downloadPath, std::ios::binary | std::ios::trunc}; if (!out) {...} }(作用域结束自动关闭) |

### 4.10 Client/login.h / login.cpp

| 位置 | 现状 | 改为 |
|------|------|------|
| login.h | Register* m_register; | std::unique_ptr<Register> m_register;(加 <memory>) |
| login.cpp 析构 | delete m_register; delete ui; | 只保留 delete ui; |
| on_pushButton_2_clicked | m_register = new Register(m_pKernel); | m_register = std::make_unique<Register>(m_pKernel); |
| on_pushButton_clicked | strcpy_s x2(L46-47) | CopyToArray x2 |
| RegisterRs 槽 | (const STRU_REGISTER_RS*)packet.constData() | reinterpret_cast<const STRU_REGISTER_RS*>(packet.constData()) |

### 4.11 Client/register.h / register.cpp

| 位置 | 现状 | 改为 |
|------|------|------|
| on_pushButton_clicked 密码强度 | for 循环 + ASCII 比较 | bool upper = std::any_of(PassWord.begin(), PassWord.end(), [](QChar c) { return c.isUpper(); });小写/数字同理 |
| on_pushButton_clicked 组包 | strcpy_s x2(L60-61) | CopyToArray x2 |
| on_pushButton_clicked 发送 | (char*)&srr | reinterpret_cast<char*>(&srr) |

### 4.12 Client/dialog.h / dialog.cpp

| 位置 | 现状 | 改为 |
|------|------|------|
| dialog.h | using namespace std; | 删除 |
| dialog.h | string m_code; | std::string m_code; |
| dialog.cpp | 无 | 无改动 |

### 4.13 Client/MD5/md5.h

| 位置 | 现状 | 改为 |
|------|------|------|
| L5-6 | typedef unsigned char byte; typedef unsigned long ulong; | using byte = std::uint8_t; using ulong = std::uint32_t;(#include <cstdint>)【正确性修复:ulong 宽度随平台,必须固定】 |
| L8-9 | using std::ifstream; using std::string; | 删除,全文 std:: 限定 |
| 成员数组 | ulong _state[4]; ulong _count[2]; byte _buffer[64]; byte _digest[16]; | std::array<ulong, 4> state_; std::array<ulong, 2> count_; std::array<byte, 64> buffer_; std::array<byte, 16> digest_;(#include <array>) |
| 静态表 | static const byte PADDING[64]; static const char HEX[16]; | inline static constexpr std::array<byte, 64> PADDING{0x80}; inline static constexpr char HEX[16] = {...};(定义并入头,删 .cpp 定义) |
| 禁拷贝 | MD5(const MD5&); MD5& operator=(const MD5&); | MD5(const MD5&) = delete; MD5& operator=(const MD5&) = delete; |
| transform 参数 | void transform(const byte block[64]); | void transform(const std::array<byte, 64>& block); |
| 流参数 | MD5(ifstream& in) / update(ifstream& in) | std::istream&(更通用) |

### 4.14 Client/MD5/md5.cpp

| 位置 | 现状 | 改为 |
|------|------|------|
| 宏 S11-S44 | #define S11 7 ... | constexpr int S11 = 7; ...(或 std::array<int, 16>) |
| 宏 UINT4 | #define UINT4 unsigned int | using UINT4 = std::uint32_t; |
| 宏 F/G/H/I、ROTATE_LEFT | #define F(x, y, z) ... | constexpr uint32_t F(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); } 等四个 + constexpr uint32_t RotateLeft(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); } |
| 宏 FF/GG/HH/II | 4 个大宏 | 4 个 constexpr 步骤函数(StepF/StepG/StepH/StepI,分别调用 F/G/H/I,内部: a += F(b,c,d)+x+ac; a = RotateLeft(a, s); a += b;) |
| transform 64 行 | FF(...) GG(...) HH(...) II(...) | StepF/StepG/StepH/StepI 调用,参数照抄 |
| transform 局部 | ulong a = ..., x[16]; | ulong a = ..., b = ..., c = ..., d = ...; std::array<ulong, 16> x{}; |
| update(ifstream&) | char buffer[BUFFER_SIZE]; ... in.close(); | std::array<char, BUFFER_SIZE> buffer; in.read(buffer.data(), buffer.size()); 删除 in.close()(不应关闭调用者的流) |
| final() | byte bits[8]; ulong oldState[4]; ulong oldCount[2]; memcpy x4 | std::array<byte, 8> bits{}; auto oldState = state_; auto oldCount = count_; std::memcpy(...) |
| 各 C 转换 | (const byte*)input、(ulong)x、(byte)(...) | reinterpret_cast / static_cast |
| bytesToHexString | int t = input[i]; int a = t / 16; int b = t % 16; | const unsigned t = input[i]; str.append(1, HEX[t >> 4]); str.append(1, HEX[t & 0x0F]); |

验证:MD5("abc") == 900150983cd24fb0d6963f7d28e17f72,空串 == d41d8cd98f00b204e9800998ecf8427e。

---

## 5. 执行顺序与验收

阶段 A:git 提交基线 -> 全量编译确认基线通过(.pro 不变)。
阶段 B:全局机械替换(第 1 节)+ packdef.h 两端同步(第 2 节)-> 编译 + 登录注册冒烟。
阶段 C:CMySQL 新接口(3.5/3.6)+ kernel.cpp SQL 与文件改造(3.3)-> 编译 + 服务端全功能冒烟。
阶段 D:kernel 单例/智能指针/上传表(3.2/3.3)+ 两端网络层(3.4/3.7-3.9、4.2-4.7)。
阶段 E:UI 层(4.8-4.12)+ MD5(4.13/4.14)+ main.cpp(3.1)-> 编译 + 手动回归。
阶段 F:零警告收尾;diff 两端 packdef.h 为空;更新 AGENTS.md/CLAUDE.md 约定(智能指针、
         无裸 new、scoped_lock、C++17);清理 Client/build、Client/release 旧产物。

手动回归清单:注册/登录/列表分页/上传(普通、秒传、断点续传、重复)/下载(md5sum 比对)/
删除(引用计数)/搜索/分享/提取/服务端退出。

---

## 6. 风险清单

| # | 风险 | 对策 |
|---|------|------|
| 1 | 协议结构体误加 std::string/vector 成员破坏布局 | 红线 1 + static_assert 锁死;评审逐结构体检查 |
| 2 | MD5 ulong 改 uint32_t 输出不变,手滑改 64 位会算错 | 标准向量自测 |
| 3 | ofstream 无返回字节数,续传进度失准 | fileinfo 采用 unique_ptr<FILE, &fclose> 方案,行为零变化 |
| 4 | Dialog unique_ptr 与 Qt parent 双重释放 | parent 传 nullptr(4.8 陷阱) |
| 5 | std::thread 未 join 即析构 -> std::terminate | 两端析构已调用 UnitNetWork/disConnectServer 且加 joinable() 检查;改动线程生命周期后必复检 |
| 6 | SQL 改用字符串拼接后引号/百分号错乱 | 外部文本仍先 EscapeString 再拼;LIKE 的 % 属格式串本身,写死即可 |
| 7 | Meyers 单例从静态期改为首次调用构造 | 语义等价;GetKernel 线程安全;main 先调 open() 保证时机 |
| 8 | reinterpret_cast 读取协议对象 | 与原有 C 强转行为一致,仅换写法 |
