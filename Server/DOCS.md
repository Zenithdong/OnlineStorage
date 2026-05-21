# 📡 服务器端模块文档

> 服务器端为 Qt Core 控制台程序，基于 Winsock2 + select() 多路复用 + MySQL C API 构建。
> 监听 `0.0.0.0:8899`，支持多客户端并发连接。

---

## 目录

- [程序入口 — main.cpp](#1-程序入口--maincpp)
- [协议定义 — packdef.h](#2-协议定义--packdefh)
- [业务层 — Kernel](#3-业务层--kernel)
- [网络层 — netWork/TCPNet](#4-网络层--networktcpnet)
- [数据库层 — CMySQL](#5-数据库层--cmysql)

---

## 1. 程序入口 — main.cpp

**文件路径:** `Server/main.cpp`

### 代码解析

```cpp
int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);    // (1) Qt 事件循环（非 GUI）
    if (!kernel::GetKernel()->open())  // (2) 单例启动：初始化网络 + 数据库
        return 1;

    getchar();                         // (3) 阻塞主线程，等待管理员回车停止
    kernel::GetKernel()->close();      // (4) 关闭所有服务
    return a.exec();
}
```

### 执行流程

| 步骤 | 操作 | 说明 |
|:---:|------|------|
| 1 | `QCoreApplication` | 创建 Qt 控制台事件循环（服务器无需 GUI） |
| 2 | `kernel::GetKernel()->open()` | 饿汉式单例初始化 → 加载 Winsock → bind 端口 8899 → 启动 select 线程 → 连接 MySQL |
| 3 | `getchar()` | 阻塞主线程，服务器持续在后台 select 线程中处理请求 |
| 4 | `kernel::GetKernel()->close()` | 设置退出标志 → 等待线程结束 → 关闭所有套接字 → 断开数据库 |

---

## 2. 协议定义 — packdef.h

**文件路径:** `Server/packdef.h`

### 包类型枚举

所有数据包通过第一个字节 `m_nType` 分发，值域来源于预处理器宏：

```
_default_protocol_base = 0         (基准值)
  ├── register_rq/rs  = 1/2       注册
  ├── login_rq/rs     = 3/4       登录
  ├── getfilelist     = 5/6       获取文件列表
  ├── uploadfileinfo  = 7/8       上传文件元信息
  ├── uploadfilecontent = 9/10    上传文件内容（分块）
  ├── deletefile      = 11/12     删除文件
  ├── downfileinfo    = 13/14     下载文件元信息
  ├── downfilecontent = 15/16     下载文件内容
  ├── selectfile      = 17/18     搜索文件
  ├── sharelink       = 19/20     分享文件
  └── getlink         = 21/22     提取分享
```

### 核心数据结构

#### 包基类 — `STRU_BASE`

```cpp
struct STRU_BASE {
    char m_nType;  // 包类型，决定分发逻辑
};
```

所有请求/响应结构体均继承自它，`m_nType` 在构造函数中自动赋值。

#### 关键业务包示例

| 包名 | 用途 | 关键字段 |
|------|------|----------|
| `STRU_REGISTER_RQ` | 注册请求 | `szName[50]`, `szpassword[50]`, `szTel` (long long) |
| `STRU_REGISTER_RS` | 注册响应 | `szResult` (0 失败 / 1 成功) |
| `STRU_LOGIN_RQ` | 登录请求 | `szName[50]`, `szpassword[50]` |
| `STRU_LOGIN_RS` | 登录响应 | `szResult` (0 失败 / 1 不存在 / 2 成功), `szUserId` |
| `STRU_UPLOADFILEINFO_RQ` | 上传元信息 | `UserId`, `szFileName`, `szFilesize`, `szFileMD5[50]` |
| `STRU_UPLOADFILEINFO_RS` | 上传元信息响应 | `szFileMD5`, `fileid`, `m_pos` (断点位置), `m_Result` (0 重复/1 续传/2 秒传/3 正常) |
| `STRU_UPLOADFILECONTENT_RQ` | 上传内容 | `userid`, `fileid`, `m_FileContent[4096]`, `m_fileNum` |
| `STRU_SHARELINK_RS` | 分享响应 | `szFileName`, `szCode[50]` (4 位随机码) |

### 结果码常量

```cpp
// 注册
_register_res_failed = 0    // 失败（用户名已存在）
_register_res_success = 1   // 成功

// 登录
_login_res_failed = 0       // 密码错误
_login_res_noexist = 1      // 用户不存在
_login_res_success = 2      // 登录成功

// 上传
_uploadfileinfo_repeat = 0  // 重复上传
_uploadfileinfo_continue = 1 // 断点续传
_uploadfileinfo_flashtrans = 2 // 秒传（MD5 匹配）
_uploadfileinfo_normal = 3  // 正常传输
```

---

## 3. 业务层 — Kernel

**文件路径:** `Server/Kernel/`

### 接口 — IKernel.h

```cpp
class IKernel {
public:
    virtual bool open() = 0;                                           // 初始化网络 + 数据库
    virtual void close() = 0;                                          // 关闭所有服务
    virtual void dealData(SOCKET socketWaiter, const char* szbuf) = 0; // 数据包分发入口
};
```

提取了三个核心生命周期方法，便于未来替换不同实现。

### 实现 — kernel (单例)

```cpp
class kernel : public IKernel {
private:
    static kernel* m_pKernel;    // 静态单例指针
    INet*   m_pNet;              // 网络层接口指针
    CMySql* m_pSql;              // 数据库层指针
    char m_szSystemPath[260];    // 文件存储根路径
    list<fileinfo*> m_lstFileInfo; // 活跃传输链表
};
```

**饿汉式单例：** 静态成员 `m_pKernel` 在程序启动时即分配，无需加锁，天然线程安全。

```cpp
kernel* kernel::m_pKernel = new kernel;  // 静态初始化阶段完成

kernel* kernel::GetKernel() {
    return m_pKernel;  // 直接返回（线程安全）
}
```

### 请求分发 — dealData()

数据包到达后，第一个字节作为 switch 键值路由：

```
szbuf[0] (即 m_nType)
  ├── 1  → RegisterRq()      注册
  ├── 3  → LoginRq()         登录
  ├── 5  → GetFileLisRq()    获取文件列表
  ├── 7  → UploadFileLisRq() 处理上传元信息
  ├── 9  → UploadFileContentRq() 接收文件内容
  ├── 11 → DeleteFileRq()    删除文件
  ├── 17 → SelectFileRq()    搜索文件
  ├── 19 → ShareLinkRq()     生成分享链接
  └── 21 → GetLinkRq()       提取分享文件
```

### 各业务函数详解

#### RegisterRq() — 用户注册

```
流程:
 ├── (1) 强转 szbuf 为 STRU_REGISTER_RQ*
 ├── (2) INSERT INTO user (name, password, tel)
 ├── (3) 查询自增 u_id
 ├── (4) CreateDirectoryA() 创建用户文件夹 disk_file/<u_id>/
 └── (5) sendData() 返回 STRU_REGISTER_RS
```

#### LoginRq() — 用户登录

```
流程:
 ├── (1) SELECT u_id, u_password FROM user WHERE u_name = ?
 ├── (2) 无记录 → _login_res_noexist
 ├── (3) 密码不匹配 → _login_res_failed
 └── (4) 密码匹配 → _login_res_success + 返回 u_id
```

#### UploadFileLisRq() — 文件上传元信息处理

这是最复杂的函数，实现了文件去重和传输策略：

```
校验 MD5 是否存在于 file 表
 ├── 存在 & 同一用户
 │   ├── 存在活跃传输记录 → _uploadfileinfo_continue (返回断点 m_pos)
 │   └── 无活跃传输 → _uploadfileinfo_repeat (重复上传)
 ├── 存在 & 其他用户上传 → _uploadfileinfo_flashtrans (秒传，引用计数 +1)
 │   └── INSERT user_file 映射 + UPDATE file.f_count + 1
 └── 不存在 → _uploadfileinfo_normal (正常传输)
     ├── INSERT file + INSERT user_file 映射
     ├── fopen("ab") 打开目标文件
     └── 将 fileinfo* 加入 m_lstFileInfo 链表
```

#### UploadFileContentRq() — 接收文件内容块

```
流程:
 ├── (1) 强转为 STRU_UPLOADFILECONTENT_RQ*
 ├── (2) 遍历 m_lstFileInfo 找到对应 fileinfo (按 fileid + userid)
 ├── (3) fwrite() 追加写入文件
 ├── (4) 更新 fileinfo.pos
 └── (5) pos == fileSize → fclose() + 从链表删除
```

#### DeleteFileRq() — 删除文件

```
流程:
 ├── (1) 查询文件 f_id, f_count, f_path
 ├── (2) DELETE FROM user_file (移除用户映射)
 ├── (3) f_count > 1 → UPDATE file SET f_count - 1 (仅减引用计数)
 └── (4) f_count == 1 → DELETE FROM file + DeleteFileA() (物理删除)
```

#### ShareLinkRq() — 文件分享

```
流程:
 ├── (1) srand(time(NULL)) 随机生成 4 位字母数字码
 ├── (2) INSERT INTO user_shared (uid, fid, code)
 └── (3) 返回 STRU_SHARELINK_RS 含提取码
```

#### GetLinkRq() — 提取分享

```
流程:
 ├── (1) SELECT uid, fid FROM user_shared WHERE code = ?
 ├── (2) uid == 请求者 ID → 失败 (不能提取自己的分享)
 ├── (3) uid != 请求者 ID → 查询文件信息 → INSERT user_file → 更新引用计数
 └── (4) 返回文件元信息
```

---

## 4. 网络层 — netWork/TCPNet

**文件路径:** `Server/netWork/`

### 接口 — INet.h

```cpp
class INet {
public:
    virtual bool InitNetWork(unsigned long dwip = 0, short nport = 8899) = 0;
    virtual void UnitNetWork() = 0;
    virtual bool sendData(SOCKET sockWaiter, const char* szbuf, int nLen) = 0;
    virtual void recvData() = 0;
};
```

### 实现 — TCPNet

```cpp
class TCPNet : public INet {
private:
    SOCKET              m_sockListen;           // 监听套接字
    list<HANDLE>        m_lstHandle;            // 工作线程句柄
    bool                m_bQuitFlag;            // 退出标志
    list<SOCKET>        m_lstSocket;            // 已连接客户端列表
    fd_set              m_fdsets;               // select 监控集合
};
```

### 初始化流程 — InitNetWork()

```
WSAStartup(2.2)
    ↓
socket(AF_INET, SOCK_STREAM, 0)
    ↓
setsockopt(SO_REUSEADDR)    ← 端口复用，支持快速重启
    ↓
bind(INADDR_ANY, 8899)
    ↓
listen(128)
    ↓
CreateThread(ThreadSelect)  ← 创建 select 线程（唯一工作线程）
    ↓
FD_SET(m_sockListen, &m_fdsets)
```

### 核心：select() 事件循环 — ThreadSelect()

```cpp
DWORD TCPNet::ThreadSelect(LPVOID lp) {
    while (m_bQuitFlag) {
        fd_set fdtemp = fdsets;              // 每次复制一份（select 会修改）
        Num = select(...&fdtemp, timeout=100us);

        while (Num > 0) {
            if (FD_ISSET(m_sockListen)) {    // 新连接
                accept() → FD_SET 加入监控集合
            } else {                         // 客户端数据
                recv(4) → 读包长
                recv(包长) → 读数据体
                kernel::dealData() → 业务处理
            }
            Num--;
        }
    }
}
```

**设计要点：**
- `select()` 超时仅 100 微秒，保证退出响应速度
- 每次循环复制 `fd_set`，因为 `select()` 会修改原集合
- 收到 `WSAECONNRESET(10054)` 表示对端断开，从客户端列表中移除

### 长度前缀协议 — sendData()

```cpp
bool TCPNet::sendData(SOCKET sockWaiter, const char* szbuf, int nLen) {
    send(sockWaiter, (char*)&nLen, sizeof(int), 0);  // 包头：4 字节长度
    send(sockWaiter, szbuf, nLen, 0);                // 包体：实际数据
}
```

### 优雅关闭 — UnitNetWork()

```
m_bQuitFlag = false
    ↓
遍历 m_lstHandle
    ├── WaitForSingleObject(100ms) → 等待线程自然退出
    └── WAIT_TIMEOUT → TerminateThread() 强制终止
    ↓
CloseHandle + 清空列表
```

---

## 5. 数据库层 — CMySQL

**文件路径:** `Server/CMySQL/`

### 类定义

```cpp
class CMySql {
public:
    bool ConnectMySql(ip, user, password, db);  // 连接数据库
    void DisConnect();                          // 断开连接
    bool SelectMysql(sql, nColumn, lst);        // SELECT 查询
    bool UpdateMysql(sql);                      // INSERT / UPDATE / DELETE
private:
    MYSQL* mysql;         // MySQL 连接对象
    MYSQL_RES* result;    // 查询结果集
    MYSQL_ROW row;        // 当前行
};
```

### ConnectMySql() — 连接数据库

```cpp
mysql_init(mysql);                                    // 初始化
mysql_set_character_set(mysql, "utf8");               // 设置 UTF-8（防中文乱码）
mysql_real_connect(mysql, ip, user, password, db, 0); // 连接（端口 0 = 默认 3306）
```

### SelectMysql() — 查询

```cpp
mysql_query(mysql, sql);                   // (1) 执行 SQL
result = mysql_store_result(mysql);        // (2) 获取结果集
while (row = mysql_fetch_row(result)) {    // (3) 逐行遍历
    for (int i = 0; i < nColumn; i++) {    // (4) 逐列读取
        lst.push_back(row[i]);             // (5) 压入 list<string>
    }
}
```

**注意：** `SelectMysql()` 返回的是扁平化的 `list<string>`，按行优先排列。调用方需按列数 `nColumn` 解析。例如查询 `SELECT u_id, u_password FROM user ...` 指定 `nColumn=2`，则 `lst.front()` 取 u_id，`lst.pop_front()` 后取 password。

### UpdateMysql() — 增删改

```cpp
mysql_query(mysql, sql);          // 执行非查询语句
```

**使用场景：** 注册 INSERT、删除 DELETE、引用计数 UPDATE、分享 INSERT 等。

---

## 数据库结构

MySQL 数据库 `server`，表结构见 `server.txt`。

| 表 | 主键 | 用途 | 关键字段 |
|------|:---:|------|------|
| `user` | u_id (自增) | 用户信息 | u_name, u_password, u_tel |
| `file` | f_id (自增) | 文件实体 | f_name, f_size, f_path, f_count (引用计数), f_md5 |
| `user_file` | (u_id, f_id) | 用户-文件映射 | u_id, f_id, time |
| `user_shared` | — | 分享链接 | uid, fid, code |
| `ufile` | — | JOIN 视图 | user_file LEFT JOIN file，简化查询 |

---

## 数据流总览

```
客户端 TCP 连接
    ↓
ThreadSelect() 检测到 select() 可读
    ↓
recv(4 字节) → 获取包长 N
    ↓
new char[N] → recv(N 字节) → 收齐数据包
    ↓
kernel::dealData(socketWaiter, pszbuf)
    ↓
switch(m_nType) → 分发到具体处理函数
    ↓
处理函数 → 拼 SQL → m_pSql->Select/Update
    ↓
处理函数 → 构建响应结构体 → m_pNet->sendData()
    ↓
send(4 字节包长) + send(响应数据)
```
