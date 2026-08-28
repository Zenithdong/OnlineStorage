#include "kernel.h"
#include "../netWork/reactor.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <system_error>

/**
 * @file kernel.cpp
 * @brief 服务端业务实现：协议分发、SQL 事务编排、文件引用关系维护与分块传输。
 *
 * 所属模块：服务端业务层（Kernel）。
 * 依赖系统库：<filesystem>（目录/文件元数据）、<fstream>（文件流读写）、<random>（提取码）。
 *
 * 并发模型：所有函数均在 Reactor 线程内被调用（单线程），无锁；见 kernel.h。
 * 存储路径与数据库连接参数硬编码（项目无配置文件风格），改动需同步两端环境。
 */

namespace {
/**
 * @brief 校验定长协议字符数组内部是否存在 '\0'。
 * @tparam Size 数组长度。
 * @param value 待校验数组。
 * @return 是否含终止符。
 * @note 数组来自不可信网络，转为 C 字符串前必须确认含 '\0'，否则 strlen/SQL 转义会越界。
 */
template <size_t Size> bool HasTerminator(const char (&value)[Size]) {
    return std::memchr(value, '\0', Size) != nullptr;
}

/**
 * @brief 校验文件名合法性（目录穿越防护）。
 * @param fileName 待校验文件名。
 * @return 是否安全。
 * @note 只接受单个文件名，拒绝 "."、".."、目录分隔符、控制字符和特殊设备路径片段；
 *       名单同时保留反斜杠与 Windows 保留字符，保证与 Windows 客户端约定一致。
 */
bool IsSafeFileName(const char* fileName) {
    if (!fileName || fileName[0] == '\0' || std::strcmp(fileName, ".") == 0 || std::strcmp(fileName, "..") == 0) {
        return false;
    }

    constexpr const char* INVALID_CHARACTERS = "\\/:*?\"<>|";
    for (const unsigned char* current = reinterpret_cast<const unsigned char*>(fileName); *current != '\0'; ++current) {
        if (*current < 32 || std::strchr(INVALID_CHARACTERS, *current)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 读取磁盘文件实际大小。
 * @param filePath 文件路径。
 * @return 字节数；文件不存在或无法访问返回 -1。
 * @note std::filesystem::file_size 的 error_code 重载不抛异常，出错时 ec 被设置、返回 -1。
 */
long long GetDiskFileSize(const std::string& filePath) {
    std::error_code ec;
    long long size = static_cast<long long>(std::filesystem::file_size(filePath, ec));
    return ec ? -1 : size;
}

/**
 * @brief 把外部字符串安全拷贝进定长协议字段。
 * @tparam N 目标数组长度（含结尾 '\0'）。
 * @param dst 目标字符数组。
 * @param src 源 C 字符串。
 * @note snprintf 保证最多写入 N-1 字节并强制以 '\0' 结尾，替代 MSVC 的 strcpy_s。
 */
template <size_t N>
void CopyToArray(char (&dst)[N], const char* src) {
    std::snprintf(dst, N, "%s", src);
}
} // namespace

/**
 * @brief 返回进程唯一业务实例（Meyers 单例）。
 * @return kernel 引用。
 * @note C++11 起静态局部变量初始化线程安全；首次调用才构造，替代原饿汉式 new。
 */
kernel& kernel::GetKernel() {
    static kernel instance;
    return instance;
}

/**
 * @brief 构造组件对象与存储根路径。
 * @note 构造函数只建对象，不占用网络/数据库资源（延迟到 open）。
 */
kernel::kernel() {
    // 构造函数只建立组件对象和存储根路径，真正占用网络与数据库资源的动作延迟到 open。
    m_pNet = std::make_unique<Reactor>();
    m_pSql = std::make_unique<CMySql>();
    m_systemPath = "/home/zenith/workspace/Online_storage/disk_file/";
}

/**
 * @brief 启动：建存储目录 -> 连数据库 -> 启监听。
 * @return 是否成功。
 * @note 顺序保证收到首个请求时磁盘与数据库均已可用；监听失败时撤销已建立的数据库连接。
 */
bool kernel::open() {
    // 启动顺序保证收到首个请求时磁盘与数据库均已可用；监听失败时主动撤销已建立的数据库连接。
    std::error_code ec;
    std::filesystem::create_directory(m_systemPath, ec);
    if (ec && ec != std::errc::file_exists) {
        std::cerr << "storage path error: " << ec.message() << std::endl;
        return false;
    }
    // 数据库密码从环境变量 MYSQL_PASSWORD 读取，避免硬编码凭据进入公开仓库。
    // 未设置该环境变量时直接启动失败，强制调用方显式提供密码。
    const char* dbPassword = std::getenv("MYSQL_PASSWORD");
    if (!dbPassword) {
        std::cerr << "MYSQL_PASSWORD environment variable not set" << std::endl;
        return false;
    }
    if (!m_pSql->ConnectMySql("127.0.0.1", "root", dbPassword, "server")) {
        std::cerr << "mysql error" << std::endl;
        return false;
    }
    if (!m_pNet->InitNetWork()) {
        std::cerr << "net error" << std::endl;
        m_pSql->DisConnect();
        return false;
    }
    return true;
}

/**
 * @brief 关闭：停网络 -> 清上传任务 -> 断数据库。
 * @note 先停网络确保清理上传表时不会再有新正文包并发进入；
 *       m_uploads 的 unique_ptr<FILE> 随容器销毁自动 fclose，磁盘中部分文件供下次续传。
 */
void kernel::close() {
    // 先停止网络线程，确保清理活动上传表时不会再有新的正文包并发进入。
    m_pNet->UnitNetWork();

    // unique_ptr<FILE> 随容器销毁自动 fclose；磁盘中的部分文件供下次上传续传。
    m_uploads.clear();

    m_pSql->DisConnect();
}

/**
 * @brief 分发入口：两级校验后调用对应请求处理函数。
 * @param socketWaiter 来源客户端 fd。
 * @param szbuf 完整包缓冲区。
 * @param len 包体长度。
 * @note 首字节选择候选结构体，再校验精确包长、字符串终止符和关键数值范围，
 *       在任何强制类型转换后的字段读取前拦截短包、非终止字符串和非法业务数据。
 */
void kernel::dealData(SOCKET socketWaiter, const char* szbuf, int len) {
    if (!szbuf || len <= 0) {
        return;
    }

    // 分发采用两级校验：首字节选择候选结构体，再验证精确包长、字符串终止符和关键数值范围。
    // 这样可以在任何强制类型转换后的字段读取前拦截短包、非终止字符串和明显非法业务数据。
    switch (*szbuf) {
    case _default_protocol_register_rq: {
        const auto* request = reinterpret_cast<const STRU_REGISTER_RQ*>(szbuf);
        if (len == sizeof(*request) && HasTerminator(request->szName) && HasTerminator(request->szpassword) &&
            request->szTel > 10000000000 && request->szTel < 19999999999) {
            RegisterRq(socketWaiter, szbuf);
        }
        break;
    }
    case _default_protocol_login_rq: {
        const auto* request = reinterpret_cast<const STRU_LOGIN_RQ*>(szbuf);
        if (len == sizeof(*request) && HasTerminator(request->szName) && HasTerminator(request->szpassword)) {
            LoginRq(socketWaiter, szbuf);
        }
        break;
    }
    case _default_protocol_getfilelist_rq: {
        const auto* request = reinterpret_cast<const STRU_GETFILELIST_RQ*>(szbuf);
        if (len == sizeof(*request) && request->szUserId > 0) {
            GetFileLisRq(socketWaiter, szbuf);
        }
        break;
    }
    case _default_protocol_uploadfileinfo_rq: {
        const auto* request = reinterpret_cast<const STRU_UPLOADFILEINFO_RQ*>(szbuf);
        if (len == sizeof(*request) && request->UserId > 0 && request->szFilesize >= 0 &&
            HasTerminator(request->szFileName) && HasTerminator(request->szFileUploadTime) &&
            HasTerminator(request->szFileMD5) && IsSafeFileName(request->szFileName)) {
            UploadFileLisRq(socketWaiter, szbuf);
        }
        break;
    }
    case _default_protocol_uploadfilecontent_rq: {
        const auto* request = reinterpret_cast<const STRU_UPLOADFILECONTENT_RQ*>(szbuf);
        if (len == sizeof(*request) && request->userid > 0 && request->fileid > 0) {
            UploadFileContentRq(socketWaiter, szbuf);
        }
        break;
    }
    case _default_protocol_selectfile_rq: {
        const auto* request = reinterpret_cast<const STRU_SELECTFILE_RQ*>(szbuf);
        if (len == sizeof(*request) && request->userid > 0 && HasTerminator(request->m_KeyWord) &&
            std::strlen(request->m_KeyWord) < MAX_SIZE) {
            SelectFileRq(socketWaiter, szbuf);
        }
        break;
    }
    case _default_protocol_deletefile_rq: {
        const auto* request = reinterpret_cast<const STRU_DELETEFILE_RQ*>(szbuf);
        if (len == sizeof(*request) && request->userId > 0 && HasTerminator(request->szFileName) &&
            IsSafeFileName(request->szFileName)) {
            DeleteFileRq(socketWaiter, szbuf);
        }
        break;
    }
    case _default_protocol_sharelink_rq: {
        const auto* request = reinterpret_cast<const STRU_SHARELINK_RQ*>(szbuf);
        if (len == sizeof(*request) && request->userId > 0 && HasTerminator(request->szFileName) &&
            IsSafeFileName(request->szFileName)) {
            ShareLinkRq(socketWaiter, szbuf);
        }
        break;
    }
    case _default_protocol_getlink_rq: {
        const auto* request = reinterpret_cast<const STRU_GETLINK_RQ*>(szbuf);
        if (len == sizeof(*request) && request->userId > 0 && HasTerminator(request->szFileUploadTime) &&
            HasTerminator(request->szCode)) {
            GetLinkRq(socketWaiter, szbuf);
        }
        break;
    }
    case _default_protocol_downloadfileinfo_rq: {
        const auto* request = reinterpret_cast<const STRU_DOWNLOADFILE_RQ*>(szbuf);
        if (len == sizeof(*request) && request->userId > 0 && HasTerminator(request->szFileName) &&
            IsSafeFileName(request->szFileName)) {
            DownLoadFileRq(socketWaiter, szbuf);
        }
        break;
    }
    default:
        break;
    }
}

/**
 * @brief 注册：创建 user 记录 + 用户磁盘目录。
 * @param socketWaiter 来源 fd。
 * @param szbuf 请求包。
 * @note SQL 注入防护：外部文本先经 EscapeString 转义再拼入；
 *       目录创建失败时删除刚插入的记录做补偿，避免产生无存储空间的无效账号。
 */
void kernel::RegisterRq(SOCKET socketWaiter, const char* szbuf) {
    // 注册业务需要同时创建 user 记录和用户磁盘目录；任何一步失败都向客户端返回统一失败结果。
    const auto* pRegisterRq = reinterpret_cast<const STRU_REGISTER_RQ*>(szbuf);
    STRU_REGISTER_RS registerRs;
    registerRs.szResult = _register_res_failed;

    std::string userName = m_pSql->EscapeString(pRegisterRq->szName);
    std::string password = m_pSql->EscapeString(pRegisterRq->szpassword);

    // 所有外部字符串先按当前 MySQL 连接字符集转义，再拼入 SQL，避免改变语句结构。
    std::string sql = "insert into user (u_name, u_password, u_tel) values ('" + userName + "', '" + password +
                      "', " + std::to_string(pRegisterRq->szTel) + ")";
    if (m_pSql->UpdateMysql(sql)) {
        unsigned long long userId = m_pSql->LastInsertId();
        if (userId > 0) {
            std::string userDirectory = m_systemPath + std::to_string(userId);
            std::error_code ec;
            std::filesystem::create_directory(userDirectory, ec);
            if (!ec || ec == std::errc::file_exists) {
                registerRs.szResult = _register_res_success;
            } else {
                // 用户目录失败时删除刚插入的记录，执行补偿以避免出现没有存储空间的无效账号。
                m_pSql->UpdateMysql("delete from user where u_id = " + std::to_string(userId));
            }
        }
    }

    // 响应结构体默认失败，所以 SQL 重复键、目录错误等所有失败分支都能得到确定反馈。
    m_pNet->sendData(socketWaiter, reinterpret_cast<char*>(&registerRs), sizeof(registerRs));
}

/**
 * @brief 登录：按用户名查询「ID + 密码」，比较后返回三种结果。
 * @param socketWaiter 来源 fd。
 * @param szbuf 请求包。
 */
void kernel::LoginRq(SOCKET socketWaiter, const char* szbuf) {
    // 登录业务按用户名查询“用户 ID + 密码”，分别返回用户不存在、密码错误或登录成功三种结果。
    const auto* pLoginRq = reinterpret_cast<const STRU_LOGIN_RQ*>(szbuf);
    STRU_LOGIN_RS loginRs;
    std::string userName = m_pSql->EscapeString(pLoginRq->szName);
    std::string sql = "select u_id, u_password from user where u_name = '" + userName + "'";
    auto rows = m_pSql->SelectMysql(sql, 2);
    if (!rows.empty()) {
        std::string userId = rows[0][0];
        std::string password = rows[0][1];

        if (password == pLoginRq->szpassword) {
            // 验证成功后把数据库主键交给客户端，作为整个会话后续文件操作的用户标识。
            std::cout << "Login success" << std::endl;
            loginRs.szResult = _login_res_success;
            loginRs.szUserId = std::stoll(userId);
        } else {
            // 账号存在但密码文本不相等，用户 ID 保持 0，避免客户端继续发起有效文件请求。
            loginRs.szResult = _login_res_failed;
            std::cout << "Password error" << std::endl;
        }
    } else {
        // 空结果集表示用户名未注册，与密码错误使用不同结果码便于界面提示。
        loginRs.szResult = _login_res_noexist;
        std::cout << "Username not exist" << std::endl;
    }
    m_pNet->sendData(socketWaiter, reinterpret_cast<char*>(&loginRs), sizeof(loginRs));
}

/**
 * @brief 文件列表：从 ufile 视图按用户 ID 查询，按页回包。
 * @param socketWaiter 来源 fd。
 * @param szbuf 请求包。
 * @note 每页最多 FILE_NUM 条，装满或耗尽即发送；发送后逐元素清空数组槽防泄漏旧数据。
 */
void kernel::GetFileLisRq(SOCKET socketWaiter, const char* szbuf) {
    // 文件列表从 ufile 视图按用户 ID 查询，查询结果每三个字符串组成一条文件摘要。
    const auto* sgr = reinterpret_cast<const STRU_GETFILELIST_RQ*>(szbuf);
    STRU_GETFILELIST_RS sgrs;
    int index = 0;
    std::string sql = "select f_name,f_size,f_uploadtime from ufile where u_id = " + std::to_string(sgr->szUserId) + ";";
    auto rows = m_pSql->SelectMysql(sql, 3);

    // 固定数组每装满 FILE_NUM 条或消耗完结果集就发送一页；客户端会把连续页追加到表格。
    for (size_t i = 0; i < rows.size(); i++) {
        CopyToArray(sgrs.arrFileInfo[index].szFileName, rows[i][0].c_str());
        CopyToArray(sgrs.arrFileInfo[index].szFileUploadTime, rows[i][2].c_str());
        sgrs.arrFileInfo[index].szFileSize = std::stoll(rows[i][1]);
        index++;
        if (index == FILE_NUM || i == rows.size() - 1) {
            sgrs.szFileNum = index;
            m_pNet->sendData(socketWaiter, reinterpret_cast<char*>(&sgrs), sizeof(sgrs));
            // 发送后清空上一页内容，防止最后一页的未使用数组槽携带旧记录。
            for (auto& info : sgrs.arrFileInfo)
                info = {};
            index = 0;
        }
    }
}

/**
 * @brief 上传协商：按 MD5 查询已有记录，选择 重复/续传/秒传/普通 四种策略。
 * @param socketWaiter 来源 fd。
 * @param szbuf 请求包。
 * @note 核心去重逻辑：
 *       - 同用户同 MD5：查活动任务续传；磁盘已完整则判重复；否则重建任务；
 *       - 他用户同 MD5 且磁盘完整：记 reusableFileId 供秒传；
 *       - 全新内容：建用户目录 + 空文件 + 事务写入 file/user_file。
 */
void kernel::UploadFileLisRq(SOCKET socketWaiter, const char* szbuf) {
    // 上传协商先按内容 MD5 查询已有记录，再结合用户归属、活动任务和磁盘大小选择上传策略。
    const auto* sur = reinterpret_cast<const STRU_UPLOADFILEINFO_RQ*>(szbuf);
    STRU_UPLOADFILEINFO_RS surs;
    CopyToArray(surs.szFileMD5, sur->szFileMD5);

    std::string fileMd5 = m_pSql->EscapeString(sur->szFileMD5);
    std::string uploadTime = m_pSql->EscapeString(sur->szFileUploadTime);
    std::string sql = "select u_id,f_id,f_path,f_size from ufile where f_md5 = '" + fileMd5 + "'";
    auto rows = m_pSql->SelectMysql(sql, 4);

    bool md5Exists = false;
    long long reusableFileId = 0;
    for (const auto& row : rows) {
        long long ownerId = std::stoll(row[0]);
        long long fileId = std::stoll(row[1]);
        std::string filePath = row[2];
        long long expectedSize = std::stoll(row[3]);
        long long diskSize = GetDiskFileSize(filePath);
        md5Exists = true;

        if (ownerId == sur->UserId) {
            surs.fileid = fileId;
            surs.m_Result = _uploadfileinfo_repeat;

            // 同一进程内的未完成任务保存精确 FILE* 和 pos，可立即告诉客户端从该位置继续。
            auto uploadIt = m_uploads.find({sur->UserId, fileId});
            if (uploadIt != m_uploads.end()) {
                surs.m_pos = uploadIt->second->pos;
                surs.m_Result = _uploadfileinfo_continue;
                m_pNet->sendData(socketWaiter, reinterpret_cast<char*>(&surs), sizeof(surs));
                return;
            }

            if (diskSize == expectedSize) {
                m_pNet->sendData(socketWaiter, reinterpret_cast<char*>(&surs), sizeof(surs));
                return;
            }

            // 重启后活动表丢失，但部分文件仍在磁盘；用实际大小重建任务，异常大小则截断并从头上传。
            const bool canContinue = diskSize >= 0 && diskSize < expectedSize;
            FILE* file = fopen(filePath.c_str(), canContinue ? "ab" : "wb");
            if (file) {
                auto info = std::make_unique<fileinfo>();
                info->userId = sur->UserId;
                info->fileId = fileId;
                info->fileSize = expectedSize;
                info->pos = canContinue ? diskSize : 0;
                info->pFile.reset(file);
                auto result = m_uploads.emplace(std::make_pair(sur->UserId, fileId), std::move(info));
                surs.m_pos = result.first->second->pos;
                surs.m_Result = canContinue ? _uploadfileinfo_continue : _uploadfileinfo_normal;
            }
            m_pNet->sendData(socketWaiter, reinterpret_cast<char*>(&surs), sizeof(surs));
            return;
        }

        // 其他用户拥有相同 MD5 时，只有磁盘大小与数据库总大小一致的实体才能作为秒传来源。
        if (reusableFileId == 0 && diskSize == expectedSize) {
            reusableFileId = fileId;
        }
    }

    if (md5Exists) {
        // 秒传不复制磁盘文件，只在事务中增加实体引用计数并插入当前用户映射，两个写操作必须同时成功。
        if (reusableFileId > 0 && m_pSql->UpdateMysql("start transaction")) {
            bool updated = m_pSql->UpdateMysql("update file set f_count = f_count + 1 where f_id = " +
                                               std::to_string(reusableFileId));
            bool mapped =
                updated && m_pSql->UpdateMysql("insert into user_file(u_id,f_id,time) values(" +
                                               std::to_string(sur->UserId) + "," + std::to_string(reusableFileId) +
                                               ",'" + uploadTime + "')");
            if (mapped && m_pSql->UpdateMysql("commit")) {
                surs.fileid = reusableFileId;
                surs.m_Result = _uploadfileinfo_flashtrans;
            } else {
                m_pSql->UpdateMysql("rollback");
            }
        }
        m_pNet->sendData(socketWaiter, reinterpret_cast<char*>(&surs), sizeof(surs));
        return;
    }

    // 全新内容先验证用户目录与目标路径，再创建空文件；数据库记录只有在文件成功创建后才进入事务。
    std::string userDirectory = m_systemPath + std::to_string(sur->UserId);
    std::string filePath = userDirectory + "/" + sur->szFileName;
    std::error_code ec;
    std::filesystem::create_directory(userDirectory, ec);
    if ((ec && ec != std::errc::file_exists) || std::filesystem::exists(filePath)) {
        m_pNet->sendData(socketWaiter, reinterpret_cast<char*>(&surs), sizeof(surs));
        return;
    }

    // wb 为普通上传建立干净目标；若事务启动失败，立即关闭并删除空文件以回滚磁盘副作用。
    FILE* file = fopen(filePath.c_str(), "wb");
    if (!file || !m_pSql->UpdateMysql("start transaction")) {
        if (file) {
            fclose(file);
            std::filesystem::remove(filePath);
        }
        m_pNet->sendData(socketWaiter, reinterpret_cast<char*>(&surs), sizeof(surs));
        return;
    }

    // file 保存实体元数据，user_file 保存当前用户对实体的拥有关系，提交后才向客户端分配 fileId。
    std::string fileName = m_pSql->EscapeString(sur->szFileName);
    std::string escapedPath = m_pSql->EscapeString(filePath);
    std::string insertFile = "insert into file(f_name,f_uploadtime,f_path,f_md5,f_size) values('" + fileName + "','" +
                             uploadTime + "','" + escapedPath + "','" + fileMd5 + "'," +
                             std::to_string(sur->szFilesize) + ")";
    bool fileInserted = m_pSql->UpdateMysql(insertFile);
    long long fileId = fileInserted ? static_cast<long long>(m_pSql->LastInsertId()) : 0;
    bool mappingInserted =
        fileId > 0 && m_pSql->UpdateMysql("insert into user_file(u_id,f_id,time) values(" +
                                          std::to_string(sur->UserId) + "," + std::to_string(fileId) + ",'" +
                                          uploadTime + "')");
    if (!mappingInserted || !m_pSql->UpdateMysql("commit")) {
        m_pSql->UpdateMysql("rollback");
        fclose(file);
        std::filesystem::remove(filePath);
        m_pNet->sendData(socketWaiter, reinterpret_cast<char*>(&surs), sizeof(surs));
        return;
    }

    // 零字节文件创建后已经完整，无需保留活动任务；非空文件则把打开句柄交给任务表等待正文块。
    surs.fileid = fileId;
    surs.m_Result = _uploadfileinfo_normal;
    if (sur->szFilesize == 0) {
        fclose(file);
    } else {
        auto info = std::make_unique<fileinfo>();
        info->userId = sur->UserId;
        info->fileId = fileId;
        info->fileSize = sur->szFilesize;
        info->pos = 0;
        info->pFile.reset(file);
        m_uploads.emplace(std::make_pair(sur->UserId, fileId), std::move(info));
    }

    m_pNet->sendData(socketWaiter, reinterpret_cast<char*>(&surs), sizeof(surs));
}

/**
 * @brief 上传正文：把一块内容写入活动任务的 FILE。
 * @param szbuf 请求包。
 * @note 通过 (uid,fid) 在 m_uploads 定位任务，阻止跨用户写入；
 *       fwrite 实际返回值才是可靠进度，累计达到总大小才关闭文件并移除任务。
 */
void kernel::UploadFileContentRq(SOCKET, const char* szbuf) {
    const auto* psus = reinterpret_cast<const STRU_UPLOADFILECONTENT_RQ*>(szbuf);

    // 正文包通过“用户 ID + 文件 ID”在活动表中定位唯一任务，避免只凭 fileId 接受跨用户写入。
    auto it = m_uploads.find({psus->userid, psus->fileid});
    if (it == m_uploads.end()) {
        return;
    }
    fileinfo* p = it->second.get();

    // 有效长度必须位于单块范围内，并且累计写入不能超过元数据声明的文件总大小。
    if (!p->pFile || psus->m_fileNum <= 0 || psus->m_fileNum > ONE_PAGE || p->pos + psus->m_fileNum > p->fileSize) {
        return;
    }

    // fwrite 的实际返回值才是可靠进度；只有累计值精确达到总大小才删除任务（unique_ptr 自动关闭文件）。
    size_t writeNum = fwrite(psus->m_FileContent, sizeof(char), psus->m_fileNum, p->pFile.get());
    if (writeNum > 0) {
        p->pos += writeNum;
        if (p->pos == p->fileSize) {
            m_uploads.erase(it);
        }
    }
}

/**
 * @brief 搜索：复用 ufile 视图按关键字 LIKE 模糊查询，分页回包。
 * @param socketWaiter 来源 fd。
 * @param szbuf 请求包。
 * @note 关键字先转义再拼入 LIKE；'%' 属格式串本身写死，非外部输入。
 */
void kernel::SelectFileRq(SOCKET socketWaiter, const char* szbuf) {
    // 搜索复用 ufile 视图并限定用户 ID，通过转义后的关键字执行文件名包含匹配。
    const auto* ssr = reinterpret_cast<const STRU_SELECTFILE_RQ*>(szbuf);
    STRU_SELECTFILE_RS ssrs;
    int index = 0;
    std::string keyword = m_pSql->EscapeString(ssr->m_KeyWord);
    std::string sql = "select f_name,f_size,f_uploadtime from ufile where u_id = " + std::to_string(ssr->userid) +
                      " and f_name like '%" + keyword + "%';";
    auto rows = m_pSql->SelectMysql(sql, 3);

    // 结果封包逻辑与完整文件列表一致：每页最多 FILE_NUM 条，连续发送直到列表耗尽。
    for (size_t i = 0; i < rows.size(); i++) {
        CopyToArray(ssrs.arrFileInfo[index].szFileName, rows[i][0].c_str());
        CopyToArray(ssrs.arrFileInfo[index].szFileUploadTime, rows[i][2].c_str());
        ssrs.arrFileInfo[index].szFileSize = std::stoll(rows[i][1]);
        index++;
        if (index == FILE_NUM || i == rows.size() - 1) {
            ssrs.szFileNum = index;
            m_pNet->sendData(socketWaiter, reinterpret_cast<char*>(&ssrs), sizeof(ssrs));
            // 清零固定数组后再装下一页，保证包中无效槽位不会泄漏上一页数据。
            for (auto& info : ssrs.arrFileInfo)
                info = {};
            index = 0;
        }
    }
}

/**
 * @brief 删除：解除用户映射，按引用计数决定是否物理删除。
 * @param szbuf 请求包。
 * @note 引用计数去重存储：f_count>1 时只减计数；==1 时删 file 记录与磁盘实体。
 */
void kernel::DeleteFileRq(SOCKET, const char* szbuf) {
    // 删除业务先从用户视图解析实体 ID、引用计数和磁盘路径，再解除当前用户映射。
    const auto* psdr = reinterpret_cast<const STRU_DELETEFILE_RQ*>(szbuf);
    std::string fileName = m_pSql->EscapeString(psdr->szFileName);
    std::string sql = "select f_id, f_count, f_path from ufile where u_id = " + std::to_string(psdr->userId) +
                      " and f_name = '" + fileName + "'";
    auto rows = m_pSql->SelectMysql(sql, 3);
    if (!rows.empty()) {
        long long fileId = std::stoll(rows[0][0]);
        long long fileCount = std::stoi(rows[0][1]);
        std::string filePath = rows[0][2];

        // user_file 表代表“用户拥有文件”的关系，删除它不会立即影响其他用户的秒传或提取结果。
        m_pSql->UpdateMysql("delete from user_file where u_id = " + std::to_string(psdr->userId) +
                            " and f_id = " + std::to_string(fileId));

        if (fileCount > 1) {
            // 仍有其他用户引用实体时只递减计数，物理文件和 file 记录继续保留。
            m_pSql->UpdateMysql("update file set f_count = f_count - 1 where f_id = " + std::to_string(fileId));
        } else {
            // 最后一个引用消失时删除 file 元数据和磁盘实体，实现引用计数式去重存储回收。
            m_pSql->UpdateMysql("delete from file where f_id = " + std::to_string(fileId));
            std::filesystem::remove(filePath);
        }
    }
}

/**
 * @brief 分享：生成四位提取码并持久化，重复分享时复用已有码。
 * @param socketWaiter 来源 fd。
 * @param szbuf 请求包。
 * @note 随机码用 std::mt19937 + uniform_int_distribution（无偏、线程局部），替代旧式 srand/rand；
 *       提取码由数据库唯一约束兜底处理并发重复。
 */
void kernel::ShareLinkRq(SOCKET socketWaiter, const char* szbuf) {
    // 分享业务为当前用户拥有的文件生成四位大写字母/数字提取码，并持久化到 user_shared。
    const auto* pssr = reinterpret_cast<const STRU_SHARELINK_RQ*>(szbuf);
    STRU_SHARELINK_RS sss;
    CopyToArray(sss.szFileName, pssr->szFileName);

    // 使用无偏随机源生成 36 进制提取码；提取码由数据库约束负责处理重复记录。
    std::mt19937 engine{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 35);
    constexpr char TABLE[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char szcode[MAX_SIZE] = {0};
    for (int i = 0; i < 4; ++i) {
        szcode[i] = TABLE[dist(engine)];
    }
    CopyToArray(sss.szCode, szcode);

    // 先通过 ufile 验证用户与文件关系；插入失败时查询该用户对该文件已有的分享码以实现重复分享复用。
    std::string fileName = m_pSql->EscapeString(pssr->szFileName);
    std::string sql = "select f_id from ufile where u_id = " + std::to_string(pssr->userId) + " and f_name = '" +
                      fileName + "';";
    auto rows = m_pSql->SelectMysql(sql, 1);
    if (!rows.empty()) {
        long long fileId = std::stoll(rows[0][0]);

        if (m_pSql->UpdateMysql("insert into user_shared(uid,fid,code) values(" + std::to_string(pssr->userId) + "," +
                                std::to_string(fileId) + ",'" + szcode + "');")) {
            CopyToArray(sss.szCode, szcode);
        } else {
            auto codeRows = m_pSql->SelectMysql("select code from user_shared where uid = " +
                                                    std::to_string(pssr->userId) + " and fid = " +
                                                    std::to_string(fileId) + ";",
                                                1);
            if (!codeRows.empty()) {
                CopyToArray(sss.szCode, codeRows[0][0].c_str());
            }
        }
    }
    // 无论新增还是复用都回送同一种响应结构体，客户端负责展示并复制提取码。
    m_pNet->sendData(socketWaiter, reinterpret_cast<char*>(&sss), sizeof(sss));
}

/**
 * @brief 提取：按分享码定位实体，为当前用户建立拥有关系。
 * @param socketWaiter 来源 fd。
 * @param szbuf 请求包。
 * @note 分享人提取自己的分享码不建新映射（避免重复引用）；
 *       映射插入 + 引用计数 +1 必须原子提交，任一失败回滚防去重引用失真。
 */
void kernel::GetLinkRq(SOCKET socketWaiter, const char* szbuf) {
    // 提取业务先按分享码定位分享人和实体文件，再为其他用户创建拥有关系。
    const auto* psgq = reinterpret_cast<const STRU_GETLINK_RQ*>(szbuf);
    STRU_GETLINK_RS sgs;
    sgs.szResult = _getlink_failed;
    sgs.szFileName[0] = '\0';
    sgs.szFileUploadTime[0] = '\0';
    sgs.szFileSize = 0;

    // 提取码属于外部输入，转义后才能进入查询；无匹配记录时响应保持默认失败。
    std::string code = m_pSql->EscapeString(psgq->szCode);
    std::string sql = "select uid, fid from user_shared where code = '" + code + "';";
    auto rows = m_pSql->SelectMysql(sql, 2);

    if (!rows.empty()) {
        long long uid = std::stoll(rows[0][0]);
        long long fid = std::stoll(rows[0][1]);

        if (uid == psgq->userId) {
            // 分享人提取自己的分享码不创建新映射，避免对同一实体重复增加引用。
            sgs.szResult = _getlink_failed;
        } else {
            // 先读取实体摘要供客户端展示，再尝试提交映射与引用计数事务。
            auto fileRows = m_pSql->SelectMysql(
                "select f_name,f_size,f_uploadtime from file where f_id = " + std::to_string(fid) + ";", 3);
            if (fileRows.size() >= 1) {
                CopyToArray(sgs.szFileName, fileRows[0][0].c_str());
                sgs.szFileSize = std::stoll(fileRows[0][1]);
                CopyToArray(sgs.szFileUploadTime, fileRows[0][2].c_str());

                // user_file 插入和 file.f_count 增加必须原子提交，任一失败都回滚，防止去重引用失真。
                std::string uploadTime = m_pSql->EscapeString(psgq->szFileUploadTime);
                if (m_pSql->UpdateMysql("start transaction")) {
                    bool mapped = m_pSql->UpdateMysql("insert into user_file(f_id, u_id, time) values(" +
                                                      std::to_string(fid) + ", " + std::to_string(psgq->userId) +
                                                      ", '" + uploadTime + "');");
                    bool countUpdated =
                        mapped && m_pSql->UpdateMysql("update file set f_count = f_count + 1 where f_id = " +
                                                      std::to_string(fid));
                    if (countUpdated && m_pSql->UpdateMysql("commit")) {
                        sgs.szResult = _getlink_success;
                    } else {
                        m_pSql->UpdateMysql("rollback");
                    }
                }
            }
        }
    }

    // 响应默认失败，只有完整事务提交后才标记成功并让客户端追加文件行。
    m_pNet->sendData(socketWaiter, reinterpret_cast<char*>(&sgs), sizeof(sgs));
}

/**
 * @brief 下载：验证归属后从 ufile 视图取物理路径，分块发送文件内容。
 * @param socketWaiter 来源 fd。
 * @param szbuf 请求包。
 * @note 用 std::ifstream 顺序读取，每块 ONE_PAGE 字节 + 有效长度发送；
 *       协议无结束标记，读到 EOF 即结束；sendData 失败（客户端断开）立即终止读取。
 */
void kernel::DownLoadFileRq(SOCKET socketWaiter, const char* szbuf) {
    // 下载先验证用户确实拥有指定文件，再从 ufile 视图取得服务端物理路径。
    const auto* psdq = reinterpret_cast<const STRU_DOWNLOADFILE_RQ*>(szbuf);
    STRU_DOWNLOADFILE_RS sds;
    std::string fileName = m_pSql->EscapeString(psdq->szFileName);
    std::string sql = "select f_path from ufile where u_id = " + std::to_string(psdq->userId) + " and f_name = '" +
                      fileName + "'";
    auto rows = m_pSql->SelectMysql(sql, 1);
    if (!rows.empty()) {
        // 文件以二进制模式顺序读取，每次把有效字节数与固定 4 KB 缓冲区一起发送给请求连接。
        std::string filePath = rows[0][0];
        std::ifstream in{filePath, std::ios::binary};
        if (!in) {
            return;
        }
        // sendData 失败通常表示客户端断开；立即终止读取，避免继续占用磁盘和网络资源。
        while (in.read(sds.m_FileContent, ONE_PAGE), (in.gcount() > 0)) {
            sds.m_fileNum = static_cast<int32_t>(in.gcount());
            if (!m_pNet->sendData(socketWaiter, reinterpret_cast<char*>(&sds), sizeof(sds))) {
                break;
            }
        }
    }
}
