#ifndef PACKDEF_H
#define PACKDEF_H

// ============================================================
// 协议包类型定义（_rq = request 请求，_rs = response 响应）
// 数据包第一字节 m_nType 用于标识包类型，接收方据此路由处理逻辑
// ============================================================
#define _default_protocol_base 0

// 注册
#define _default_protocol_register_rq       _default_protocol_base + 1
#define _default_protocol_register_rs       _default_protocol_base + 2

// 登录
#define _default_protocol_login_rq          _default_protocol_base + 3
#define _default_protocol_login_rs          _default_protocol_base + 4

// 获取文件列表
#define _default_protocol_getfilelist_rq    _default_protocol_base + 5
#define _default_protocol_getfilelist_rs    _default_protocol_base + 6

// 上传文件信息（文件名、大小等元数据）
#define _default_protocol_uploadfileinfo_rq _default_protocol_base + 7
#define _default_protocol_uploadfileinfo_rs _default_protocol_base + 8

// 上传文件内容（实际文件数据分块传输）
#define _default_protocol_uploadfilecontent_rq  _default_protocol_base + 9
#define _default_protocol_uploadfilecontent_rs  _default_protocol_base + 10

// 删除文件
#define _default_protocol_deletefile_rq     _default_protocol_base + 11
#define _default_protocol_deletefile_rs     _default_protocol_base + 12

// 下载文件信息（文件名、大小等元数据）
#define _default_protocol_downloadfileinfo_rq   _default_protocol_base + 13
#define _default_protocol_downloadfileinfo_rs   _default_protocol_base + 14

// 下载文件内容（实际文件数据分块传输）
#define _default_protocol_downfilecontent_rq    _default_protocol_base + 15
#define _default_protocol_downfilecontent_rs    _default_protocol_base + 16

//查询文件
#define _default_protocol_selectfile_rq _default_protocol_base + 17
#define _default_protocol_selectfile_rs _default_protocol_base + 18

//分享
#define _default_protocol_sharelink_rq _default_protocol_base + 19
#define _default_protocol_sharelink_rs _default_protocol_base + 20

//提取
#define _default_protocol_getlink_rq _default_protocol_base + 21
#define _default_protocol_getlink_rs _default_protocol_base + 22

// 字符串字段的最大长度（用户名、密码等）
#define MAX_SIZE 50
// 文件列表中最大文件数量
#define FILE_NUM 15
// SQL 语句最大长度
#define SQLLEN 300
// 文件路径最大长度
#define FILE_PATH 260
//一页大小
#define ONE_PAGE 4096

// ============================================================
// 注册结果码
// ============================================================
#define _register_res_failed    0   // 注册失败（用户名已存在等）
#define _register_res_success   1   // 注册成功

// ============================================================
// 登录结果码
// ============================================================
#define _login_res_failed   0   // 登录失败（密码错误）
#define _login_res_noexist  1   // 用户不存在
#define _login_res_success  2   // 登录成功

//传输结果
#define _uploadfileinfo_repeat 0 //重复上传
#define _uploadfileinfo_continue 1 //断点续传
#define _uploadfileinfo_flashtrans 2 //秒传
#define _uploadfileinfo_normal 3 //正常传

//提取链接
#define _getlink_failed 0
#define _getlink_success 1
// ============================================================
// 协议包结构体定义
// 所有包均继承自 STRU_BASE，通过 m_nType 区分包类型
// 网络传输时直接将结构体内存序列化发送（需注意字节对齐和平台一致性）
// ============================================================

// 协议包基类：仅含包类型字段
struct STRU_BASE {
    char m_nType;  // 包类型，对应上方 _default_protocol_* 宏
};

// 注册请求包
struct STRU_REGISTER_RQ : public STRU_BASE {
    STRU_REGISTER_RQ() {
        m_nType = _default_protocol_register_rq;
    }
    char      szName[MAX_SIZE];      // 用户名
    char      szpassword[MAX_SIZE];  // 密码
    long long szTel;                 // 手机号
};

// 注册响应包
struct STRU_REGISTER_RS : public STRU_BASE {
    STRU_REGISTER_RS() {
        m_nType = _default_protocol_register_rs;
    }
    char szResult;  // 注册结果码，参见 _register_res_* 宏
};

// 登录请求包
struct STRU_LOGIN_RQ : public STRU_BASE {
    STRU_LOGIN_RQ() {
        m_nType = _default_protocol_login_rq;
    }
    char szName[MAX_SIZE];      // 用户名
    char szpassword[MAX_SIZE];  // 密码
};

// 登录响应包
struct STRU_LOGIN_RS : public STRU_BASE {
    STRU_LOGIN_RS() {
        m_nType = _default_protocol_login_rs;
    }
    char      szResult;   // 登录结果码，参见 _login_res_* 宏
    long long szUserId;   // 登录成功时返回的用户 ID
};

//获取文件列表
struct STRU_GETFILELIST_RQ : public STRU_BASE {
    STRU_GETFILELIST_RQ() {
        m_nType = _default_protocol_getfilelist_rq;
    }
    long long szUserId;
};

//查找是不是文件信息列表（链表）
struct FileInfo {
    //文件名   文件上传时间  文件大小
    char szFileName[MAX_SIZE];
    char szFileUploadTime[MAX_SIZE];
    long long szFileSize;
};

//获取文件列表响应包
struct STRU_GETFILELIST_RS : public STRU_BASE {
    STRU_GETFILELIST_RS() {
        m_nType = _default_protocol_getfilelist_rs;
    }
    FileInfo arrFileInfo[FILE_NUM];
    long long szFileNum; //此数据包内真正的文件个数
};

struct STRU_UPLOADFILEINFO_RQ : public STRU_BASE{
    STRU_UPLOADFILEINFO_RQ() {
        m_nType = _default_protocol_uploadfileinfo_rq;
    }
    long long UserId;
    char szFileName[MAX_SIZE];
    char szFileUploadTime[MAX_SIZE];
    long long szFilesize;
    char szFileMD5[MAX_SIZE];   //MD5:根据文件信息与文件内容生成的一个唯一标识
};

struct STRU_UPLOADFILEINFO_RS:public STRU_BASE{
    STRU_UPLOADFILEINFO_RS(){
        m_nType = _default_protocol_uploadfileinfo_rs;
    }
    char szFileMD5[MAX_SIZE];
    long long fileid;   //文件的id
    long long m_pos;    //文件的传输位置，传了一半停止了
    char m_Result;  //传输结果
};

//上传文件内容
struct STRU_UPLOADFILECONTENT_RQ : public STRU_BASE {
    STRU_UPLOADFILECONTENT_RQ(){
        m_nType = _default_protocol_uploadfilecontent_rq;
    }
    long long userid;
    long long fileid;   //文件的id
    char m_FileContent[ONE_PAGE];   //文件内容数组
    long m_fileNum;     //真正上传文件的大小
};

//搜索文件
struct STRU_SELECTFILE_RQ:public STRU_BASE{
    STRU_SELECTFILE_RQ() {
        m_nType = _default_protocol_selectfile_rq;
    }
    long long userid;
    char m_KeyWord[ONE_PAGE];

};

struct STRU_SELECTFILE_RS:public STRU_BASE{
    STRU_SELECTFILE_RS() {
        m_nType = _default_protocol_selectfile_rs;
    }
    FileInfo arrFileInfo[FILE_NUM];
    long szFileNum; //此数据包内真正的文件个数
};

struct STRU_DELETEFILE_RQ:public STRU_BASE{
    STRU_DELETEFILE_RQ() {
        m_nType = _default_protocol_deletefile_rq;
    }
    long long userId;
    char szFileName[MAX_SIZE];
};

//分享  随机码由服务器生成
struct STRU_SHARELINK_RQ:public STRU_BASE{
    STRU_SHARELINK_RQ() {
        m_nType = _default_protocol_sharelink_rq;
    }
    long long userId;
    char szFileName[MAX_SIZE];
};

struct STRU_SHARELINK_RS:public STRU_BASE{
    STRU_SHARELINK_RS() {
        m_nType = _default_protocol_sharelink_rs;
    }
    char szFileName[MAX_SIZE];
    char szCode[MAX_SIZE];
};

//提取链接
struct STRU_GETLINK_RQ:public STRU_BASE{
    STRU_GETLINK_RQ() {
        m_nType = _default_protocol_getlink_rq;
    }
    long long userId;
    char szFileUploadTime[MAX_SIZE];
    char szCode[MAX_SIZE];
};

struct STRU_GETLINK_RS:public STRU_BASE{
    STRU_GETLINK_RS() {
        m_nType = _default_protocol_getlink_rs;
    }
    char szFileName[MAX_SIZE];
    char szFileUploadTime[MAX_SIZE];
    long long szFileSize;
    char szResult;
};

//下载文件
struct STRU_DOWNLOADFILE_RS : public STRU_BASE {
    STRU_DOWNLOADFILE_RS(){
        m_nType = _default_protocol_downloadfileinfo_rs;
    }
    char m_FileContent[ONE_PAGE];//文件内容数组
    long m_fileNum; //真正上传文件大小
};

struct STRU_DOWNLOADFILE_RQ : public STRU_BASE {
    STRU_DOWNLOADFILE_RQ(){
        m_nType = _default_protocol_downloadfileinfo_rq;
    }
    long long userId;
    char szFileName[MAX_SIZE];
};


#endif // PACKDEF_H
