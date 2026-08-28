# AGENTS.md — Online Storage 工作指南

语言：请用中文回答用户。

## 构建

```bash
# 服务器（Qt Core 控制台）
cd Server && qmake Server.pro && mingw32-make

# 客户端（Qt Widgets GUI）
cd Client && qmake Client.pro && mingw32-make
```

- Qt **6.11.0** MinGW 64-bit, C++17, **qmake**（不是 cmake）
- 修改 `.pro` 文件后**必须重新运行 `qmake`**
- 服务器需要 MySQL C API 库（`libmysql`）可链接
- 无自动化测试，手动验证

## 关键约束

- **`Client/packdef.h` 和 `Server/packdef.h` 必须内容完全一致**（二进制协议，结构体内存直接序列化）
- 网络层 Windows-only（Winsock2，`CreateThread`，`CreateDirectoryA`，`DeleteFileA`）
- 内存管理：原始 `new[]`/`delete[]`，无智能指针

## 硬编码配置（无配置文件）

| 配置 | 位置 |
|------|------|
| 数据库 `root:<MYSQL_PASSWORD>@127.0.0.1:server` | `Server/Kernel/kernel.cpp`（`open()`） |
| 服务器监听 `0.0.0.0:8899` | `Server/netWork/tcpnet.cpp`（`InitNetWork`） |
| 客户端连接 `127.0.0.1:8899` | `Client/Kernel/IKernel.h:13` |
| 存储根路径 | `Server/Kernel/kernel.cpp:12`（`m_szSystemPath`） |

## 服务器单例

```
static kernel* kernel::m_pKernel = new kernel;  // 饿汉式，静态初始化时创建
```

文件上传跟踪：`list<fileinfo*>`（`m_lstFileInfo`），每个活跃传输一个堆分配的 `fileinfo`。

## 客户端架构

```
MainWindow  ──Qt 信号槽(Qt::BlockingQueuedConnection)──  Kernel(QObject+IKernel)
                                                            ↓
                                                         tcpnet(Winsock+CreateThread)
```

- `MainWindow` 构造函数中连接所有信号槽
- 接收线程 `ThreadRecv` 使用 `CreateThread`（非 QThread）
- `Kernel::DealData()` 根据 `m_nType` switch 发射对应信号

## 协议约定

- 传输：4 字节长度前缀（int32）+ 结构体内存
- 分块：`ONE_PAGE=4096`（上传/下载均按 4KB 分块）
- 文件列表分页：`FILE_NUM=15` 条/页
- 结果码：`_uploadfileinfo_repeat=0`, `_continue=1`, `_flashtrans=2`, `_normal=3`

## 添加新协议步骤

1. 两端 `packdef.h` 同步：定义类型宏 + 结构体
2. 服务端 `kernel::dealData()` switch 加 case → 实现 `*Rq()` 方法
3. 客户端 `Kernel::DealData()` switch 加 case → 发射信号
4. `Kernel` 头文件声明新信号
5. `MainWindow` 构造函数连接信号到槽，实现槽函数

## 其他参考

- 已有 `CLAUDE.md`（根目录和 `Client/` 下各一份）包含架构和数据库详情
- `Server/DOCS.md` / `Client/DOCS.md` 含模块级代码分析
