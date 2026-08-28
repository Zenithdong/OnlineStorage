#ifndef KERNEL_H
#define KERNEL_H
#pragma once
#include "IKernel.h"
#include "../netWork/INet.h"
#include "../CMySQL/cmysql.h"
#include "packdef.h"
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>

/**
 * @brief FILE 的函数对象 deleter。
 *
 * 相比函数指针 deleter（unique_ptr<FILE, decltype(&fclose)>），函数对象可默认构造，
 * 使 fileinfo 能直接被 make_unique 创建（函数指针 deleter 的默认构造被 unique_ptr 禁止，
 * 因会得到 null deleter）。operator() 内部判空后调用 std::fclose。
 */
struct FileCloser {
    void operator()(FILE* f) const {
        if (f) {
            std::fclose(f);
        }
    }
};

/**
 * @brief 活动上传任务：把协议中的用户/文件标识与已打开的磁盘文件关联。
 *
 * 正文包不重复携带路径和总大小，服务端通过该表校验归属、限制写入边界并维护续传偏移。
 *
 * 生命周期：由 kernel::m_uploads（map<pair<uid,fid>, unique_ptr<fileinfo>>）独占持有，
 * 上传完成或服务关闭时从容器移除即析构；pFile 用 unique_ptr 保证 FILE* 的 RAII 关闭。
 */
struct fileinfo {
    std::unique_ptr<FILE, FileCloser> pFile; ///< 以追加/写入模式打开的目标文件句柄（RAII）。
    long long userId = 0;                    ///< 上传发起人，用于阻止其他用户向该任务写数据。
    long long fileId = 0;                    ///< file 表主键，正文请求匹配任务的主要标识。
    long long fileSize = 0;                  ///< 元数据声明的最终大小，用于拒绝越界写入。
    long long pos = 0;                       ///< 已成功写入字节数，也是断点续传起点。
};

/**
 * @brief 服务端业务中枢：校验协议、访问 MySQL、维护文件引用关系、执行分块文件 I/O。
 *
 * 所属模块：服务端业务层（Kernel）。
 * 依赖系统库：<filesystem>（目录/文件元数据，C++17）、<cstdio>（FILE/fopen/fwrite）。
 *
 * 并发模型：Meyers 单例（C++11 静态局部变量线程安全初始化）。
 * 所有请求处理都在 Reactor 线程内被调用（reactor.cpp 的 handler 回调），
 * 因此 m_uploads、m_pSql 无锁——单线程模型天然串行（原「多线程共享单 MYSQL 连接」隐患被消除）。
 */
class kernel : public IKernel {
public:
    /// @brief 构造组件对象与存储根路径；真正占用资源延迟到 open()。
    kernel();

    /// @brief 析构：m_pNet/m_pSql/m_uploads 由 unique_ptr 自动释放。
    ~kernel() = default;

public:
    /**
     * @brief 返回进程唯一业务实例（Meyers 单例）。
     * @return kernel 引用。
     * @note 首次调用时才构造，C++11 保证静态局部变量初始化线程安全。
     */
    static kernel& GetKernel();

    /**
     * @brief 按「存储目录 -> MySQL -> TCP 监听」顺序启动。
     * @return 是否成功；任一依赖未就绪即返回 false，避免在依赖未就绪时接收请求。
     */
    bool open();

    /**
     * @brief 按依赖反向关闭：停网络 -> 清上传任务 -> 断数据库。
     */
    void close();

    /**
     * @brief 分发入口：对不可信网络包做结构体大小、字符串终止符、业务范围校验后再处理。
     * @param socketWaiter 来源客户端 fd。
     * @param szbuf 完整包缓冲区。
     * @param len 包体长度。
     */
    void dealData(SOCKET socketWaiter, const char* szbuf, int len);

    // 以下处理函数分别实现注册、登录、列表、上传协商、上传正文、搜索、删除、分享、提取和下载。
    void RegisterRq(SOCKET socketWaiter, const char* szbuf);

    void LoginRq(SOCKET socketWaiter, const char* szbuf);

    void GetFileLisRq(SOCKET socketWaiter, const char* szbuf);

    void UploadFileLisRq(SOCKET socketWaiter, const char* szbuf);

    void UploadFileContentRq(SOCKET socketWaiter, const char* szbuf);

    void SelectFileRq(SOCKET socketWaiter, const char* szbuf);

    void DeleteFileRq(SOCKET socketWaiter, const char* szbuf);

    void ShareLinkRq(SOCKET socketWaiter, const char* szbuf);

    void GetLinkRq(SOCKET socketWaiter, const char* szbuf);

    void DownLoadFileRq(SOCKET socketWaiter, const char* szbuf);

private:
    std::unique_ptr<INet> m_pNet;   ///< 实际指向 Reactor，负责长度前缀协议与多客户端连接。
    std::unique_ptr<CMySql> m_pSql; ///< MySQL C API 封装（查询/更新/转义/自增 ID）。
    std::string m_systemPath;       ///< 实体文件存储根目录（堆内存，每位用户以 ID 建子目录）。
    std::map<std::pair<long long, long long>, std::unique_ptr<fileinfo>> m_uploads; ///< 未完成上传任务，键为 (uid,fid)。
};

#endif
