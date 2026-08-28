#ifndef PACKDEF_H
#define PACKDEF_H

#include <cstdint>

/**
 * @file packdef.h
 * @brief 客户端与服务端共享的二进制协议定义（两端必须逐字一致）。
 *
 * 所属模块：客户端/服务端协议层。
 *
 * 线协议约定：每个 TCP 帧先发送 4 字节包长，再直接发送下列结构体的内存；
 * 结构体首字段 m_nType 标识类型，接收方据此把缓冲区强制转换为对应结构体。
 *
 * 布局约束（关键）：该协议依赖相同的编译器、字节序、类型宽度与对齐规则，因此：
 * - 结构体只允许 char 数组与定宽整数成员，禁止 std::string/vector 等类类型成员；
 * - 跨平台宽度有差异的字段统一用 int32_t（如上传/下载块长度 m_fileNum、分页条数 szFileNum）；
 * - 两端本文件必须逐字一致，改完一端需复制到另一端并 diff 校验。
 */
#define _default_protocol_base 0

// 用户注册：请求提交账号、密码和手机号，响应返回注册结果。
#define _default_protocol_register_rq _default_protocol_base + 1
#define _default_protocol_register_rs _default_protocol_base + 2

// 用户登录：请求校验账号密码，成功响应携带后续文件业务使用的用户 ID。
#define _default_protocol_login_rq _default_protocol_base + 3
#define _default_protocol_login_rs _default_protocol_base + 4

// 文件列表：登录成功后查询当前用户拥有的文件，响应按 FILE_NUM 分页返回。
#define _default_protocol_getfilelist_rq _default_protocol_base + 5
#define _default_protocol_getfilelist_rs _default_protocol_base + 6

// 上传协商：先提交文件元数据，服务端再决定重复、断点续传、秒传或普通上传。
#define _default_protocol_uploadfileinfo_rq _default_protocol_base + 7
#define _default_protocol_uploadfileinfo_rs _default_protocol_base + 8

// 上传正文：仅普通上传和断点续传需要按 ONE_PAGE 大小发送内容块；当前实现没有正文响应包。
#define _default_protocol_uploadfilecontent_rq _default_protocol_base + 9
#define _default_protocol_uploadfilecontent_rs _default_protocol_base + 10

// 删除文件：解除用户与文件的映射；服务端根据引用计数决定是否删除实体文件。
#define _default_protocol_deletefile_rq _default_protocol_base + 11
#define _default_protocol_deletefile_rs _default_protocol_base + 12

// 下载文件：请求指定用户文件，响应连续返回内容块；名称沿用早期协议中的 fileinfo。
#define _default_protocol_downloadfileinfo_rq _default_protocol_base + 13
#define _default_protocol_downloadfileinfo_rs _default_protocol_base + 14

// 预留的下载正文类型编号，当前业务直接使用 downloadfileinfo 响应传送每个内容块。
#define _default_protocol_downfilecontent_rq _default_protocol_base + 15
#define _default_protocol_downfilecontent_rs _default_protocol_base + 16

// 文件搜索：在当前用户拥有的文件中按名称关键字模糊查询。
#define _default_protocol_selectfile_rq _default_protocol_base + 17
#define _default_protocol_selectfile_rs _default_protocol_base + 18

// 创建分享：服务端为用户拥有的文件生成或复用四位提取码。
#define _default_protocol_sharelink_rq _default_protocol_base + 19
#define _default_protocol_sharelink_rs _default_protocol_base + 20

// 提取分享：使用提取码为当前用户建立文件映射，并增加实体文件引用计数。
#define _default_protocol_getlink_rq _default_protocol_base + 21
#define _default_protocol_getlink_rs _default_protocol_base + 22

// 定长字符字段保留末尾 '\0'，实际 UTF-8 内容必须小于 MAX_SIZE 字节。
#define MAX_SIZE 50
// 列表与搜索响应每包最多容纳 15 条记录，更多结果通过连续响应包发送。
#define FILE_NUM 15
// 服务端拼装 SQL 使用的栈缓冲区大小；动态文本写入前仍需进行数据库转义。
#define SQLLEN 1024
// Windows 文件路径和客户端本地路径使用的定长缓冲区大小。
#define FILE_PATH 260
// 上传和下载的固定分块缓冲区为 4096 字节，有效长度由 m_fileNum 单独说明。
#define ONE_PAGE 4096

// 注册结果：failed 包含用户名/手机号重复或服务端落库失败，success 表示用户记录和目录均已创建。
#define _register_res_failed 0
#define _register_res_success 1

// 登录结果：区分密码不匹配、用户不存在和验证成功，便于界面给出准确反馈。
#define _login_res_failed 0
#define _login_res_noexist 1
#define _login_res_success 2

// 上传协商结果：客户端据此决定提示重复、从 m_pos 续传、直接完成秒传或从头发送正文。
#define _uploadfileinfo_repeat 0
#define _uploadfileinfo_continue 1
#define _uploadfileinfo_flashtrans 2
#define _uploadfileinfo_normal 3
#define _uploadfileinfo_failed 4

// 提取结果：success 表示用户映射与实体引用计数已在同一事务中提交。
#define _getlink_failed 0
#define _getlink_success 1

// 所有业务包继承 STRU_BASE，默认成员初始化既设置包类型，也把定长数组清零，避免发送未初始化内存。
struct STRU_BASE {
    char m_nType = _default_protocol_base; // 协议号固定处于结构体首字节，接收方无需先转换具体类型即可分发。
};

// 注册请求承载新用户凭据；手机号以整数传输，因此无法保留前导零。
struct STRU_REGISTER_RQ : public STRU_BASE {
    STRU_REGISTER_RQ() {
        m_nType = _default_protocol_register_rq;
    }
    char szName[MAX_SIZE]{};     // UTF-8 用户名，服务端转义后写入 user 表。
    char szpassword[MAX_SIZE]{}; // 当前业务直接传输的密码文本，不具备网络加密能力。
    long long szTel = 0;         // 经过客户端和服务端范围校验的 11 位手机号。
};

// 注册响应只返回状态；失败的具体数据库原因不会通过协议暴露给客户端。
struct STRU_REGISTER_RS : public STRU_BASE {
    STRU_REGISTER_RS() {
        m_nType = _default_protocol_register_rs;
    }
    char szResult = _register_res_failed; // 默认失败，只有数据库记录与用户目录都成功时改为 success。
};

// 登录请求提交用户输入；服务端按用户名查找记录后比较密码。
struct STRU_LOGIN_RQ : public STRU_BASE {
    STRU_LOGIN_RQ() {
        m_nType = _default_protocol_login_rq;
    }
    char szName[MAX_SIZE]{};     // 登录用户名。
    char szpassword[MAX_SIZE]{}; // 登录密码。
};

// 登录响应在成功时返回数据库用户主键，失败时 szUserId 保持为 0。
struct STRU_LOGIN_RS : public STRU_BASE {
    STRU_LOGIN_RS() {
        m_nType = _default_protocol_login_rs;
    }
    char szResult = _login_res_failed; // 取值来自 _login_res_*，默认按密码错误处理。
    long long szUserId = 0;            // 后续所有文件请求用于标识当前用户的业务 ID。
};

// 文件列表请求只携带登录用户 ID，服务端从 ufile 视图查询该用户拥有的文件。
struct STRU_GETFILELIST_RQ : public STRU_BASE {
    STRU_GETFILELIST_RQ() {
        m_nType = _default_protocol_getfilelist_rq;
    }
    long long szUserId = 0; // 当前登录用户的数据库主键。
};

// 列表、搜索和提取响应复用的文件摘要；不暴露服务端物理路径和文件 ID。
struct FileInfo {
    char szFileName[MAX_SIZE]{};       // 用户界面展示及后续业务请求使用的文件名。
    char szFileUploadTime[MAX_SIZE]{}; // 用户获得该文件时记录的时间文本。
    long long szFileSize = 0;          // 文件总字节数。
};

// 文件列表响应是固定容量分页包，szFileNum 指明数组中本次真正有效的元素数。
struct STRU_GETFILELIST_RS : public STRU_BASE {
    STRU_GETFILELIST_RS() {
        m_nType = _default_protocol_getfilelist_rs;
    }
    FileInfo arrFileInfo[FILE_NUM]{}; // 本页文件摘要，未使用的槽位保持清零。
    long long szFileNum = 0;          // 有效元素数量，取值范围为 0 到 FILE_NUM。
};

// 上传协商请求描述文件身份与元数据，此阶段不携带文件正文。
struct STRU_UPLOADFILEINFO_RQ : public STRU_BASE {
    STRU_UPLOADFILEINFO_RQ() {
        m_nType = _default_protocol_uploadfileinfo_rq;
    }
    long long UserId = 0;              // 发起上传的登录用户 ID。
    char szFileName[MAX_SIZE]{};       // 服务器逻辑文件名，同时作为用户目录下的新文件名。
    char szFileUploadTime[MAX_SIZE]{}; // 客户端发起上传的时间文本。
    long long szFilesize = 0;          // 完整文件字节数，用于校验续传进度和完成条件。
    char szFileMD5[MAX_SIZE]{};        // 文件内容摘要，用于判重、秒传和关联客户端待处理任务。
};

// 上传协商响应告诉客户端接下来是否以及从哪里发送文件正文。
struct STRU_UPLOADFILEINFO_RS : public STRU_BASE {
    STRU_UPLOADFILEINFO_RS() {
        m_nType = _default_protocol_uploadfileinfo_rs;
    }
    char szFileMD5[MAX_SIZE]{};             // 原样回传请求摘要，客户端用它匹配本地任务。
    long long fileid = 0;                   // 服务端 file 表主键，后续正文块据此定位活动任务。
    long long m_pos = 0;                    // 续传时服务端已经写入的字节数，普通上传为 0。
    char m_Result = _uploadfileinfo_failed; // 上传策略码，默认失败可覆盖所有提前返回路径。
};

// 上传正文请求固定携带 4 KB 缓冲区，最后一块通常不足 4 KB，必须以 m_fileNum 为有效长度。
struct STRU_UPLOADFILECONTENT_RQ : public STRU_BASE {
    STRU_UPLOADFILECONTENT_RQ() {
        m_nType = _default_protocol_uploadfilecontent_rq;
    }
    long long userid = 0;           // 与活动上传任务匹配的用户 ID，防止跨用户写入。
    long long fileid = 0;           // 与活动上传任务匹配的文件 ID。
    char m_FileContent[ONE_PAGE]{}; // 当前文件块的原始二进制数据。
    int32_t m_fileNum = 0;          // 当前块有效字节数，范围为 1 到 ONE_PAGE。
};

// 搜索请求限定当前用户，并提交文件名关键字；虽然缓冲区较大，业务层仍限制实际文本小于 MAX_SIZE。
struct STRU_SELECTFILE_RQ : public STRU_BASE {
    STRU_SELECTFILE_RQ() {
        m_nType = _default_protocol_selectfile_rq;
    }
    long long userid = 0;          // 当前登录用户 ID。
    char m_KeyWord[ONE_PAGE]{};    // 服务端 LIKE 查询使用的关键字。
};

// 搜索结果与文件列表采用相同分页思路，但历史协议中的计数字段类型为 long。
struct STRU_SELECTFILE_RS : public STRU_BASE {
    STRU_SELECTFILE_RS() {
        m_nType = _default_protocol_selectfile_rs;
    }
    FileInfo arrFileInfo[FILE_NUM]{}; // 本页匹配文件摘要。
    int32_t szFileNum = 0;            // 本页有效数量，取值范围为 0 到 FILE_NUM。
};

// 删除请求只解除当前用户对指定文件的拥有关系，不代表实体文件一定立即删除。
struct STRU_DELETEFILE_RQ : public STRU_BASE {
    STRU_DELETEFILE_RQ() {
        m_nType = _default_protocol_deletefile_rq;
    }
    long long userId = 0;              // 当前登录用户 ID。
    char szFileName[MAX_SIZE]{};       // 当前用户列表中的目标文件名。
};

// 分享请求要求服务端确认用户拥有该文件，然后创建或复用分享记录。
struct STRU_SHARELINK_RQ : public STRU_BASE {
    STRU_SHARELINK_RQ() {
        m_nType = _default_protocol_sharelink_rq;
    }
    long long userId = 0;              // 分享发起人的用户 ID。
    char szFileName[MAX_SIZE]{};       // 要分享的文件名。
};

// 分享响应返回文件名和可复制的提取码；空提取码表示分享创建失败。
struct STRU_SHARELINK_RS : public STRU_BASE {
    STRU_SHARELINK_RS() {
        m_nType = _default_protocol_sharelink_rs;
    }
    char szFileName[MAX_SIZE]{}; // 对应请求中的文件名，用于提示用户。
    char szCode[MAX_SIZE]{};     // 服务端生成或查询得到的四位提取码。
};

// 提取请求提交分享码和当前用户信息，时间将写入新的 user_file 映射。
struct STRU_GETLINK_RQ : public STRU_BASE {
    STRU_GETLINK_RQ() {
        m_nType = _default_protocol_getlink_rq;
    }
    long long userId = 0;                   // 接收共享文件的用户 ID。
    char szFileUploadTime[MAX_SIZE]{};      // 当前用户获得共享文件的时间。
    char szCode[MAX_SIZE]{};                // 待查询的分享提取码。
};

// 提取响应在成功时携带文件摘要，客户端据此把新获得的文件追加到列表。
struct STRU_GETLINK_RS : public STRU_BASE {
    STRU_GETLINK_RS() {
        m_nType = _default_protocol_getlink_rs;
    }
    char szFileName[MAX_SIZE]{};       // 被提取的文件名。
    char szFileUploadTime[MAX_SIZE]{}; // 文件原始上传时间，用于当前界面显示。
    long long szFileSize = 0;          // 被提取文件总字节数。
    char szResult = _getlink_failed;   // 失败也覆盖重复拥有、无效提取码或数据库事务失败。
};

// 下载响应每包承载一个内容块；当前协议没有总大小、偏移和结束标记，客户端依赖顺序写入。
struct STRU_DOWNLOADFILE_RS : public STRU_BASE {
    STRU_DOWNLOADFILE_RS() {
        m_nType = _default_protocol_downloadfileinfo_rs;
    }
    char m_FileContent[ONE_PAGE]{}; // 当前下载块的原始二进制数据。
    int32_t m_fileNum = 0;          // 当前块有效字节数，字段名沿用上传协议。
};

// 下载请求通过“用户 ID + 文件名”解析文件，客户端不能直接提交服务端物理路径。
struct STRU_DOWNLOADFILE_RQ : public STRU_BASE {
    STRU_DOWNLOADFILE_RQ() {
        m_nType = _default_protocol_downloadfileinfo_rq;
    }
    long long userId = 0;              // 当前登录用户 ID，用于校验文件归属。
    char szFileName[MAX_SIZE]{};       // 当前用户文件列表中的目标名称。
};

#endif
