# 🖥️ 客户端模块文档

> 客户端为 Qt Widgets GUI 程序，采用三层架构：UI 层（Qt 窗体）→ 业务层（Kernel·信号槽）→ 网络层（Tcpnet·Winsock 接收线程）。

---

## 目录

- [程序入口 — main.cpp](#1-程序入口--maincpp)
- [协议定义 — packdef.h](#2-协议定义--packdefh)
- [UI 层 — 窗体模块](#3-ui-层--窗体模块)
- [业务层 — Kernel](#4-业务层--kernel)
- [网络层 — Tcpnet](#5-网络层--tcpnet)
- [MD5 模块](#6-md5-模块)

---

## 1. 程序入口 — main.cpp

**文件路径:** `Client/main.cpp`

```cpp
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);  // Qt GUI 事件循环
    MainWindow w;                 // 创建主窗口（默认隐藏）
    return a.exec();
}
```

`MainWindow` 构造函数中完成网络连接和信号槽绑定，构造完毕即进入 Qt 事件循环。

---

## 2. 协议定义 — packdef.h

**文件路径:** `Client/packdef.h`

内容与服务器端 `Server/packdef.h` 完全一致，必须同步维护。

### 核心协议速查

| 宏 | 值 | 方向 | 用途 |
|------|:---:|:---:|------|
| `_default_protocol_register_rq/rs` | 1/2 | 客户端→服务端/RSP | 用户注册 |
| `_default_protocol_login_rq/rs` | 3/4 | 客户端→服务端/RSP | 用户登录 |
| `_default_protocol_getfilelist_rq/rs` | 5/6 | 客户端→服务端/RSP | 获取文件列表 |
| `_default_protocol_uploadfileinfo_rq/rs` | 7/8 | 客户端→服务端/RSP | 上传文件元信息 |
| `_default_protocol_uploadfilecontent_rq/rs` | 9/10 | 客户端→服务端/RSP | 上传文件内容（分块 4KB） |
| `_default_protocol_deletefile_rq/rs` | 11/12 | 客户端→服务端/RSP | 删除文件 |
| `_default_protocol_selectfile_rq/rs` | 17/18 | 客户端→服务端/RSP | 搜索文件 |
| `_default_protocol_sharelink_rq/rs` | 19/20 | 客户端→服务端/RSP | 生成分享链接 |
| `_default_protocol_getlink_rq/rs` | 21/22 | 客户端→服务端/RSP | 提取分享文件 |

### 关键宏常量

```cpp
#define MAX_SIZE   50    // 字符串字段最大长度（文件名、用户名、密码等）
#define FILE_NUM   15    // 单页文件列表最大条目数
#define FILE_PATH  260   // 文件路径最大长度
#define ONE_PAGE   4096  // 文件内容分块大小（4KB）
#define SQLLEN     300   // SQL 语句最大长度
```

---

## 3. UI 层 — 窗体模块

### 3.1 Login — 登录对话框

**文件:** `Client/login.{h,cpp,ui}`

- UI 通过 Qt Designer 设计（`login.ui`）
- 用户输入用户名和密码后，填充 `STRU_LOGIN_RQ` 结构体
- 调用 `IKernel::SendData()` 发送登录请求
- 提供 `RegisterRs` 公共槽函数，接收注册结果并弹窗提示
- `m_pKernel` 指针通过构造函数注入，与 MainWindow 共享同一个 Kernel 实例

**数据流：**

```
用户点击「登录」
  ↓
Login::on_button_clicked()
  ↓
填充 STRU_LOGIN_RQ{szName, szpassword}
  ↓
m_pKernel->SendData(&rq, sizeof(rq))
  ↓
... 网络传输 ...
  ↓
Kernel::DealData() 发射 LoginRs(szbuf) 信号
  ↓
MainWindow::LoginRs(szbuf) 槽函数处理
```

### 3.2 Register — 注册对话框

**文件:** `Client/register.{h,cpp,ui}`

- 收集用户名、密码、手机号
- 填充 `STRU_REGISTER_RQ` 结构体
- 通过 `m_pKernel->SendData()` 发送

### 3.3 MainWindow — 主窗口

**文件:** `Client/mainwindow.{h,cpp,ui}`

#### 构造函数启动流程

```
构造 MainWindow
  ├── new Kernel                 创建业务层实例
  ├── Kernel::Connect()          连接服务器 127.0.0.1:8899
  ├── new Login(m_pKernel)       创建登录对话框
  ├── Login::show()              显示登录界面
  └── 绑定信号槽                 注册所有业务信号的接收函数
```

#### 信号-槽映射表

所有连接使用 `Qt::BlockingQueuedConnection`，确保网络线程回传数据与 UI 线程同步：

| Kernel 信号 | MainWindow 槽 | 功能 |
|------|------|------|
| `LoginRs` | `MainWindow::LoginRs` | 处理登录结果 |
| `RegisterRs` | `Login::RegisterRs` (转发到 Login) | 处理注册结果 |
| `GetFileLisRs` | `MainWindow::GetFileLisRs` | 显示文件列表 |
| `UploadFileInfoRs` | `MainWindow::UploadFileInfoRS` | 处理上传元信息响应 |
| `SelectFileRs` | `MainWindow::SelectFileRs` | 显示搜索结果 |
| `ShareLinkRs` | `MainWindow::ShareLinkRs` | 显示分享码 |
| `GetLinkRs` | `MainWindow::GetLinkRs` | 显示提取的文件 |

#### LoginRs() — 登录结果处理

```
szbuf → STRU_LOGIN_RS*
  ├── szResult == _login_res_failed → QMessageBox "用户名或密码错误"
  ├── szResult == _login_res_noexist → QMessageBox "用户不存在"
  └── szResult == _login_res_success
      ├── m_pLogin->hide()         隐藏登录窗口
      ├── this->show()             显示主窗口
      ├── 记录 Id = sls->szUserId
      └── 发送 STRU_GETFILELIST_RQ 获取文件列表
```

#### GetFileLisRs() — 文件列表展示

```
szbuf → STRU_GETFILELIST_RS*
  └── 遍历 szFileNum 条记录
      ├── tableWidget->insertRow()
      └── setItem() 填入文件名、文件大小、上传时间
```

#### on_action_2_triggered() — 上传文件按钮

```
QFileDialog::getOpenFileName()          用户选择文件
  ↓
QFile::size()                           获取文件大小
  ↓
FileDigest(filePath)                    计算 MD5（流式读文件）
  ↓
QDateTime::currentDateTime()            获取当前时间
  ↓
new uploadFileInfo 加入链表             记录文件元信息（供后续分块发送用）
  ↓
发送 STRU_UPLOADFILEINFO_RQ             请求上传
```

#### UploadFileInfoRS() — 上传元信息响应处理

```
szbuf → STRU_UPLOADFILEINFO_RS*
  ├── 遍历 m_lstuploadFileInfo 找到对应文件信息
  └── switch(m_Result)
      ├── _uploadfileinfo_repeat    → QMessageBox "你已经上传过文件"
      ├── _uploadfileinfo_flashtrans → 显示成功（秒传无需发内容）
      ├── _uploadfileinfo_continue  → fopen + fseek(m_pos) 断点续传
      └── _uploadfileinfo_normal    → fopen 正常传输
          ↓
      循环发送 STRU_UPLOADFILECONTENT_RQ（4KB 分块）
      fread → scr.m_FileContent[4096] → SendData
      ↓
      最后一个分块发送完毕后 fclose()
```

#### on_Select_clicked() — 搜索文件

```
ui->lineEdit→text()                    获取搜索关键词
  ↓
STRU_SELECTFILE_RQ {userid, m_KeyWord} 构建请求
  ↓
m_pKernel->SendData()                  发送搜索请求
  ↓
ui->tableWidget->setRowCount(0)        清空当前列表
```

#### on_Delete_clicked() — 删除文件

```
tableWidget->currentRow()              获取选中行
  ↓
取 fileName = item()->text()
  ↓
STRU_DELETEFILE_RQ {userId, fileName}  构建请求
  ↓
SendData → 成功后 removeRow(nRow)      发送请求并从列表中移除
```

#### ShareLinkRs() — 分享结果展示

```
szbuf → STRU_SHARELINK_RS*
  └── QMessageBox 显示文件名 + 提取码
      ├── "Copy Code" 按钮（可复制提取码）
      └── "OK" 按钮
```

#### on_actionSend_File_triggered() — 提取分享

```
弹出 Dialog 对话框                  用户输入提取码和时间
  ↓
填充 STRU_GETLINK_RQ
  ↓
SendData → GetLinkRs 槽处理结果
```

#### GetLinkRs() — 提取结果处理

```
szbuf → STRU_GETLINK_RS*
  ├── szResult == _getlink_failed → 提示"此文件您已拥有"
  └── szResult == _getlink_success → tableWidget 添加文件记录
```

### 3.4 Dialog — 分享提取对话框

**文件:** `Client/dialog.{h,cpp,ui}`

- 简单的 QDialog，用户在此输入提取码
- 返回 `QDialog::Accepted` 时 MainWindow 读取 `m_code` 字段发送提取请求

---

## 4. 业务层 — Kernel

**文件路径:** `Client/Kernel/`

### 接口 — IKernel.h

```cpp
class IKernel {
public:
    virtual bool Connect(szip="127.0.0.1", nport=8899) = 0;  // 连接服务器
    virtual void DisConnect() = 0;                              // 断开连接
    virtual bool SendData(szbuf, Len) = 0;                      // 发送数据包
    virtual void DealData(szbuf) = 0;                           // 接收数据分发
};
```

### 实现 — Kernel (QObject + IKernel)

```cpp
class Kernel : public QObject, public IKernel {
    Q_OBJECT
public:
    bool Connect(...);      // 委托给 m_pNet->ConnectServer()
    bool SendData(...);     // 委托给 m_pNet->SendData()
    void DealData(...);     // switch(m_nType) → emit 对应信号

signals:
    void LoginRs(const char*);
    void RegisterRs(const char*);
    void GetFileLisRs(const char*);
    void UploadFileInfoRs(const char*);
    void UploadFileContentRs(const char*);
    void SelectFileRs(const char*);
    void ShareLinkRs(const char*);
    void GetLinkRs(const char*);

private:
    INet* m_pNet;  // 网络层接口
};
```

### DealData() — 响应数据包分发

```cpp
void Kernel::DealData(const char *szbuf) {
    switch (*szbuf) {                       // 第一个字节即 m_nType
        case _default_protocol_login_rs:    emit LoginRs(szbuf);            break;
        case _default_protocol_register_rs: emit RegisterRs(szbuf);         break;
        case _default_protocol_getfilelist_rs: emit GetFileLisRs(szbuf);    break;
        case _default_protocol_uploadfileinfo_rs: emit UploadFileInfoRs(szbuf); break;
        case _default_protocol_selectfile_rs: emit SelectFileRs(szbuf);     break;
        case _default_protocol_sharelink_rs: emit ShareLinkRs(szbuf);       break;
        case _default_protocol_getlink_rs: emit GetLinkRs(szbuf);           break;
    }
}
```

**设计要点：** Kernel 同时继承 `QObject`（用于发射信号）和 `IKernel`（业务接口），采用多重继承实现"收发双向解耦"——发送走 IKernel 方法，接收走 Qt 信号机制。

---

## 5. 网络层 — Tcpnet

**文件路径:** `Client/Tcpnet/`

### 接口 — INet.h

```cpp
class INet {
public:
    virtual bool ConnectServer(szip, nport) = 0;  // 连接服务器
    virtual void disConnectServer() = 0;           // 断开连接
    virtual bool SendData(szbuf, nLen) = 0;        // 发送数据
    virtual void RecvData() = 0;                   // 接收数据
};
```

### 实现 — tcpnet

```cpp
class tcpnet : public INet {
    SOCKET  sockclient;       // 客户端套接字
    bool    m_bQuitfFlag;     // 接收线程退出标志
    HANDLE  m_hThread;        // 接收线程句柄
    IKernel* m_pKernel;       // 回传数据到业务层
};
```

### ConnectServer() — 连接流程

```
WSAStartup(2.2)
    ↓
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
    ↓
inet_addr(szip) → sockaddr_in {AF_INET, port, ip}
    ↓
connect(sockclient, addrserver)                    ← 三次握手，阻塞等待
    ↓
CreateThread(&ThreadRecv)                          ← 创建独立接收线程
```

### SendData() — 长度前缀协议

```cpp
bool tcpnet::SendData(const char *szbuf, int nLen) {
    send(sockclient, (char*)&nLen, sizeof(int), 0);  // 包头：4 字节长度
    send(sockclient, szbuf, nLen, 0);                 // 包体：实际数据
}
```

与服务器 `TCPNet::sendData()` 完全对称。

### RecvData() — 接收数据

```cpp
void tcpnet::RecvData() {
    recv(sockclient, &nPackageSize, sizeof(int));  // (1) 读包长
    char *pszbuf = new char[nPackageSize];          // (2) 分配缓冲区
    while (nPackageSize) {                          // (3) 循环收齐完整包
        nRecvNum = recv(sockclient, pszbuf + noffset, nPackageSize);
        noffset += nRecvNum;
        nPackageSize -= nRecvNum;
    }
    m_pKernel->DealData(pszbuf);                    // (4) 交给业务层
    delete[] pszbuf;                                 // (5) 释放缓冲区
}
```

### ThreadRecv() — 接收线程

```cpp
DWORD tcpnet::ThreadRecv(LPVOID lp) {
    tcpnet* pthis = (tcpnet*)lp;
    while (pthis->m_bQuitfFlag) {  // 循环接收，直到退出标志为 false
        pthis->RecvData();          // 每次接收一个完整数据包
    }
    return 0;
}
```

**设计要点：**
- Win32 `CreateThread` 而非 QThread，避免与 Qt 事件循环耦合
- 循环体内每次处理一个完整数据包，处理完才进入下一轮 recv
- 收到的数据通过 `m_pKernel->DealData()` 进入 Kernel，Kernel 内部 emit 信号到 UI 线程

### disConnectServer() — 断开

```
m_bQuitfFlag = false                     通知接收线程退出
    ↓
WaitForSingleObject(100ms)              等待线程自然退出
    ↓
WAIT_TIMEOUT → TerminateThread()        超时强制终止
```

---

## 6. MD5 模块

**文件路径:** `Client/MD5/`

### 类概述

```cpp
class MD5 {
public:
    MD5();                          // 空构造 → reset()
    MD5(string &str);               // 字符串 MD5
    MD5(ifstream &in);              // 文件流 MD5
    void update(input, length);     // 增量更新（支持流式处理）
    string toString();              // 返回 32 位小写十六进制字符串
private:
    ulong _state[4];                // ABCD 四个 32 位状态寄存器
    byte  _digest[16];              // 最终 128 位摘要
    void transform(block[64]);      // 64 字节块变换（四轮 64 步）
};
```

### 标准 MD5 实现

实现了 RFC 1321 标准的 MD5 算法：
- 4 个 32 位状态寄存器（A=0x67452301, B=0xEFCDAB89, C=0x98BADCFE, D=0x10325476）
- 四个轮函数 F/G/H/I，每轮 16 步，共 64 步
- 支持流式 `update()` 增量处理，适用于大文件
- `toString()` 返回小写十六进制字符串

### 使用方式 — MainWindow::FileDigest()

```cpp
string MainWindow::FileDigest(const string &file) {
    ifstream in(file, std::ios::binary);
    MD5 md5;
    char buffer[1024];
    while (!in.eof()) {
        in.read(buffer, 1024);
        if (in.gcount() > 0)
            md5.update(buffer, in.gcount());
    }
    return md5.toString();  // 返回 32 位小写 hex
}
```

每次读 1KB 数据块流式喂入 MD5，避免将整个文件加载到内存。

---

## 客户端数据流总览

```
UI 操作 (用户点击按钮)
  ↓ 填充结构体
  ↓ m_pKernel->SendData()
  ↓
Kernel::SendData() → tcpnet::SendData()
  ↓
send(4 B) + send(数据体)
  ↓ ═══ TCP 网络 ═══
  ↓
服务器处理...
  ↓ ═══ TCP 网络 ═══
  ↓
tcpnet::ThreadRecv() 持续循环
  ↓ recv(4 B) → 读取包长
  ↓ recv(N B) → 读取数据体
  ↓
m_pKernel->DealData(pszbuf)
  ↓
Kernel::DealData() → switch(m_nType) → emit Signal
  ↓ Qt::BlockingQueuedConnection
  ↓
MainWindow 槽函数
  ↓ 强制转换缓冲区为对应结构体
  ↓ 更新 UI 控件
```

## struct uploadFileInfo 说明

`MainWindow` 内部维护的自定义结构体，用于跟踪上传文件状态：

```cpp
struct uploadFileInfo {
    char szFilePath[FILE_PATH];       // 本地文件完整路径
    char szFileUploadTime[MAX_SIZE];  // 上传时间字符串
    long long m_szFileSize;           // 文件大小
    long long m_pos;                  // 断点位置（当前已上传字节数）
    char szFileMD5[MAX_SIZE];         // MD5 哈希值
};
```

上传时创建并加入 `m_lstuploadFileInfo` 链表。收到服务器 `STRU_UPLOADFILEINFO_RS` 时，通过 MD5 匹配找到对应 `uploadFileInfo`，读取本地文件分块发送。上传完成后从链表中删除。
