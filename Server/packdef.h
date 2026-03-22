#ifndef PACKDEF_H
#define PACKDEF_H

// 声明数据包类型
#define _default_protocol_base 0
// 注册
#define _default_protocol_register_rq _default_protocol_base + 1
#define _default_protocol_register_rs _default_protocol_base + 2
// 登录
#define _default_protocol_login_rq _default_protocol_base + 3
#define _default_protocol_login_rs _default_protocol_base + 4
// 获取文件列表
#define _default_protocol_getfilelist_rq _default_protocol_base + 5
#define _default_protocol_getfilelist_rs _default_protocol_base + 6
// 上传文件信息
#define _default_protocol_uploadfileinfo_rq _default_protocol_base + 7
#define _default_protocol_uploadfileinfo_rs _default_protocol_base + 8
// 上传文件内容
#define _default_protocol_uploadfilecontent_rq _default_protocol_base + 9
#define _default_protocol_uploadfilecontent_rs _default_protocol_base + 10
// 删除文件
#define _default_protocol_deletefile_rq _default_protocol_base + 11
#define _default_protocol_deletefile_rs _default_protocol_base + 12
// 下载文件信息
#define _default_protocol_downfileinfo_rq _default_protocol_base + 13
#define _default_protocol_downfileinfo_rs _default_protocol_base + 14
// 下载文件内容
#define _default_protocol_downfilecontent_rq _default_protocol_base + 15
#define _default_protocol_downfilecontent_rs _default_protocol_base + 16

#define MAX_SIZE 50

// 注册结果
#define _register_res_failed 0
#define _register_res_success 1

// 登录结果
#define _login_res_failed 0
#define _login_res_noexist 1
#define _login_res_success 2

// 协议包基类
struct STRU_BASE {
    char m_nType;
};

// 注册请求
struct STRU_REGISTER_RQ : public STRU_BASE {
    STRU_REGISTER_RQ() {
        m_nType = _default_protocol_register_rq;
    }
    char szName[MAX_SIZE];
    char szpassword[MAX_SIZE];
    long long szTel;
};

// 注册响应
struct STRU_REGISTER_RS : public STRU_BASE {
    STRU_REGISTER_RS() {
        m_nType = _default_protocol_register_rs;
    }
    char szResult;
};

// 登录请求
struct STRU_LOGIN_RQ : public STRU_BASE {
    STRU_LOGIN_RQ() {
        m_nType = _default_protocol_login_rq;
    }
    char szName[MAX_SIZE];
    char szpassword[MAX_SIZE];
};

// 登录响应
struct STRU_LOGIN_RS : public STRU_BASE {
    STRU_LOGIN_RS() {
        m_nType = _default_protocol_login_rs;
    }
    char szResult;
    long long szUserId;
};

#endif // PACKDEF_H
