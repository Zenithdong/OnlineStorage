#include "kernel.h"
#include <iostream>

// 饿汉式单例：程序启动时即完成实例化，天然线程安全
kernel* kernel::m_pKernel = new kernel;

kernel::kernel()
{
    // 创建网络层和数据库层对象（此时尚未连接，需调用 open() 完成初始化）
    m_pNet = new TCPNet;
    m_pSql = new CMySql;
    strcpy(m_szSystemPath, "D:/_Workspace/DevProjects/Online_storage/disk_file/"); // 设置系统路径
}

kernel::~kernel()
{
    delete m_pNet;
    m_pNet = NULL;

    delete m_pSql;
    m_pSql = NULL;
}

// 返回全局唯一 kernel 实例
kernel* kernel::GetKernel()
{
    // 饿汉式单例，m_pKernel 在静态初始化阶段已创建，此处直接返回
    return m_pKernel;
}

// 启动服务：初始化网络监听和数据库连接
bool kernel::open()
{
    // 初始化 TCP 网络层，使用默认端口 8899
    if (!m_pNet->InitNetWork()) {
        std::cerr << "net error" << std::endl;
        return false;
    }
    // 连接本地 MySQL，数据库名为 server
    if (!m_pSql->ConnectMySql("127.0.0.1", "root", "1114", "server")) {
        std::cerr << "mysql error" << std::endl;
        return false;
    }
    return true;
}

// 关闭服务：停止网络线程并断开数据库连接
void kernel::close()
{
    m_pNet->UnitNetWork();
    m_pSql->DisConnect();
}

// 处理客户端数据包：根据 szbuf 中的包类型字段分发到对应业务处理函数
void kernel::dealData(SOCKET socketWaiter, const char* szbuf)
{
    // 待实现：解析 STRU_BASE::m_nType，路由到注册、登录、文件操作等处理逻辑
    switch (*szbuf)
    {
    case _default_protocol_register_rq:
        //客户端注册请求，调用 RegisterRq 处理
        RegisterRq(socketWaiter, szbuf);
        break;
    case _default_protocol_login_rq:
        //客户端登录请求，调用 LoginRq 处理
        LoginRq(socketWaiter, szbuf);
        break;
    case _default_protocol_getfilelist_rq:
        GetFileLisRq(socketWaiter, szbuf);
        break;
    case _default_protocol_uploadfileinfo_rq:
        UploadFileLisRq(socketWaiter, szbuf);
        break;
    case _default_protocol_uploadfilecontent_rq:
        UploadFileContentRq(socketWaiter, szbuf);
        break;
    case _default_protocol_selectfile_rq:
        SelectFileRq(socketWaiter, szbuf);
        break;
    case _default_protocol_deletefile_rq:
        DeleteFileRq(socketWaiter, szbuf);
        break;
    case _default_protocol_sharelink_rq:
        ShareLinkRq(socketWaiter, szbuf);
        break;
    case _default_protocol_getlink_rq:
        GetLinkRq(socketWaiter, szbuf);
        break;
    case _default_protocol_downloadfileinfo_rq:
        DownLoadFileRq(socketWaiter, szbuf);
        break;
    default:
        break;
    }
}

void kernel::RegisterRq(SOCKET socketWaiter, const char *szbuf)
{
    // 解析注册请求数据包，提取用户名、密码和手机号
    //写入数据库，执行 INSERT 语句
    //成功,失败
    STRU_REGISTER_RQ* pRegisterRq = (STRU_REGISTER_RQ*)szbuf;  // 强制类型转换为注册请求包结构体指针，便于访问包内字段
    char szsql[SQLLEN] = { 0 };                                // 存储 SQL 语句的缓冲区，长度由 SQLLEN 宏定义
    STRU_REGISTER_RS registerRs;                               // 注册响应包结构体，用于封装注册结果并发送给客户端
    registerRs.szResult = _register_res_failed;                // 默认注册结果为失败，后续根据数据库操作结果更新为成功或保持失败状态
    std::list<std::string> lsStr;                              // 用于存储数据库查询结果的列表，后续用于获取新注册用户的 ID 以创建对应文件夹
    char FilePath[FILE_PATH] = { 0 };                          // 存储用户文件夹路径的缓冲区，长度由 FILE_PATH 宏定义
    // 构造 SQL 插入语句，将新用户信息写入数据库
    sprintf(szsql, "insert into user (u_name, u_password, u_tel) values ('%s', '%s', %lld)", pRegisterRq->szName, pRegisterRq->szpassword, pRegisterRq->szTel);
    if (m_pSql->UpdateMysql(szsql)) {
        // 注册成功
        registerRs.szResult = _register_res_success;
        // 获取用户 ID（自增主键）,根据id创建文件夹
        sprintf(szsql, "select u_id from user where u_name = '%s' and u_password = '%s'", pRegisterRq->szName, pRegisterRq->szpassword);
        m_pSql->SelectMysql(szsql, 1, lsStr);
        if(lsStr.size() > 0) {
            // 从查询结果中获取用户 ID，并在系统路径下创建以用户 ID 命名的文件夹
            std::string UserId = lsStr.front();                         // 获取查询结果列表中的第一个元素，即新注册用户的 ID
            lsStr.pop_front();                                          // 从列表中移除已获取的用户 ID，保持列表清洁
            sprintf(FilePath, "%s%s", m_szSystemPath, UserId.c_str());  // 构造用户文件夹的完整路径，格式为 "系统路径 + 用户ID"
            CreateDirectoryA(FilePath, 0);                              // 调用 Windows API 创建目录，参数为用户文件夹路径和安全属性（此处为 NULL）
        }
    }
    // 将注册结果封装到响应包中，并发送给客户端
    m_pNet->sendData(socketWaiter, (char*)&registerRs, sizeof(registerRs));
}

void kernel::LoginRq(SOCKET socketWaiter, const char *szbuf)
{
    // 根据用户名查询密码，三种情况：用户名不存在、密码错误、登录成功
    STRU_LOGIN_RQ* pLoginRq = (STRU_LOGIN_RQ*)szbuf;  // 强制类型转换为登录请求包结构体指针，便于访问包内字段
    STRU_LOGIN_RS loginRs;                            // 登录响应包结构体，用于封装登录结果并发送给客户端
    char szsql[SQLLEN] = { 0 };                       // 存储 SQL 语句的缓冲区，长度由 SQLLEN 宏定义
    std::list<std::string> lsStr;                     // 用于存储数据库查询结果的列表，后续用于验证登录信息
    sprintf(szsql, "select u_id, u_password from user where u_name = '%s'", pLoginRq->szName);  // 构造 SQL 查询语句，根据用户名查询对应的密码
    m_pSql->SelectMysql(szsql, 2, lsStr);             // 执行 SQL 查询，将结果存储在 lsStr 列表中
    if(lsStr.size() > 0) {
        // 用户名存在，验证密码
        std::string userId = lsStr.front();           // 获取查询结果列表中的第一个元素，即数据库中存储的用户ID
        lsStr.pop_front();                            // 从列表中移除已获取的用户ID

        std::string password = lsStr.front();         // 获取查询结果列表中的第一个元素，即数据库中存储的密码
        lsStr.pop_front();                            // 从列表中移除已获取的密码

        if(strcmp(password.c_str(), pLoginRq->szpassword) == 0) {
            // 登录成功
            std::cout << "Login success" << std::endl;
            loginRs.szResult = _login_res_success;
            loginRs.szUserId = _atoi64(userId.c_str());  // 将用户 ID 转换为 long long 类型，并存储在登录响应包中，供客户端后续使用
        } else {
            // 密码错误
            loginRs.szResult = _login_res_failed;   // 设置登录结果为密码错误
            std::cout << "Password error" << std::endl;
        }
    } else {
        // 用户名不存在
        loginRs.szResult = _login_res_noexist;      // 设置登录结果为用户名不存在
        std::cout << "Username not exist" << std::endl;
    }
    m_pNet->sendData(socketWaiter, (char*)&loginRs, sizeof(loginRs));
}

void kernel::GetFileLisRq(SOCKET socketWaiter, const char *szbuf){
    STRU_GETFILELIST_RQ* sgr = (STRU_GETFILELIST_RQ*)szbuf;
    STRU_GETFILELIST_RS sgrs;
    char szsql[SQLLEN] = {0};
    int index = 0;
    list<string> lsStr;
    //看是那个人获取文件信息
    sprintf(szsql,"select f_name,f_size,f_uploadtime from ufile where u_id = %lld;",sgr->szUserId);
    //数据查询此人上传过的文件
    m_pSql->SelectMysql(szsql,3,lsStr);
    //封装进GetFileListRS结构体中
    while(lsStr.size()>0){
        string fileName = lsStr.front();
        lsStr.pop_front();

        string fileSize = lsStr.front();
        lsStr.pop_front();

        string fileTime = lsStr.front();
        lsStr.pop_front();

        strcpy_s(sgrs.arrFileInfo[index].szFileName,fileName.c_str());
        strcpy_s(sgrs.arrFileInfo[index].szFileUploadTime,fileTime.c_str());
        sgrs.arrFileInfo[index].szFileSize = atoll(fileSize.c_str());
        index++;
        if(index == FILE_NUM||lsStr.size() == 0){ //文件信息数组中已经包含15个文件 链表所有数据已经取出来了
            sgrs.szFileNum = index;
            //返还给客户端
            m_pNet->sendData(socketWaiter,(char*)&sgrs,sizeof(sgrs));
            ZeroMemory(sgrs.arrFileInfo,sizeof(sgrs.arrFileInfo));  //内存清空
            index = 0;
        }
    }
    //返还给客户端
}

void kernel::UploadFileLisRq(SOCKET socketWaiter, const char *szbuf)
{
    //1、校验数据库中是否存在文件
    STRU_UPLOADFILEINFO_RQ* sur = (STRU_UPLOADFILEINFO_RQ*)szbuf;
    char szsql[SQLLEN] = {0};
    list<string> lsStr;
    STRU_UPLOADFILEINFO_RS surs;
    surs.m_pos = 0;
    strcpy_s(surs.szFileMD5,sur->szFileMD5);
    sprintf(szsql,"select u_id,f_id from ufile where f_MD5 = '%s'", sur->szFileMD5);
    m_pSql->SelectMysql(szsql, 2, lsStr);
    bool flag = false;
    string FileId;
    auto ite = lsStr.begin();
    while(ite != lsStr.end()) {
        string UserId = *ite;
        long long Uid = atoll(UserId.c_str());
        ite++;
        FileId = *ite;
        ite++;
        if(Uid == sur->UserId) {
            flag = true;
            //1、白己传的重复上传
            surs.m_Result = _uploadfileinfo_repeat;
            surs.fileid = atoll(FileId.c_str());
            //链表
            auto Fileite = m_lstFileInfo.begin();
            while(Fileite!= m_lstFileInfo.end()){
                if((*Fileite)->fileId == atoll(FileId.c_str()) && sur->UserId == (*Fileite)->userId){
                    //断点续传
                    surs.m_pos = (*Fileite)->pos;
                    surs.m_Result = _uploadfileinfo_continue;
                    break;
                }
                Fileite++;
            }
        }
    }
    if(lsStr.size() > 0 && flag == false){
            surs.m_Result = _uploadfileinfo_flashtrans;
            //2、别人传的基于引用计数实现的   秒传  计数+1

            sprintf(szsql,"update file set f_count = f_count + 1 where f_id = %lld", atoll(FileId.c_str()));
            m_pSql->UpdateMysql(szsql);
            //修改映射
            sprintf(szsql,"insert into user_file(u_id,f_id,time) values(%lld,%lld,'%s')",
                    sur->UserId,atoll(FileId.c_str()),sur->szFileUploadTime);
            m_pSql->UpdateMysql(szsql);

    }
    if(lsStr.size() == 0){
        surs.m_Result = _uploadfileinfo_normal;
        //3、如果文件不存在正常传输记录文件信息到数据库，映射表做映射
        char szFilePath[FILE_PATH]={0};
        sprintf(szFilePath,"%s%lld/%s",m_szSystemPath,sur->UserId,sur->szFileName);
        sprintf(szsql,"insert into file(f_name,f_uploadtime,f_path,f_md5,f_size) values('%s','%s','%s','%s' ,%lld)",
                sur->szFileName,sur->szFileUploadTime,szFilePath,sur->szFileMD5,sur->szFilesize);
        m_pSql->UpdateMysql(szsql);
        //查找f_id
        sprintf(szsql,"select f_id from file where f_md5 = '%s'",sur->szFileMD5);
        m_pSql->SelectMysql(szsql,1,lsStr);
        if(lsStr.size()>0){
            string FileId = lsStr.front();
            lsStr.pop_front();
            surs.fileid = atoll(FileId.c_str());
            //修改映射
            sprintf(szsql,"insert into user_file(u_id,f_id,time) values(%lld,%lld,'%s')",
                    sur->UserId,atoll(FileId.c_str()),sur->szFileUploadTime);
            m_pSql->UpdateMysql(szsql);
        }
        FILE* pFile = fopen(szFilePath,"ab");
        //记录文件指针，文件ID，userid,filesize, pos
        fileinfo *p = new fileinfo;
        p->userId = sur->UserId;
        p->fileId = surs.fileid;
        p->fileSize = sur->szFilesize;
        p->pos = 0;
        p->pFile = pFile;
        m_lstFileInfo.push_back(p);
    }
    //4、发送回复
    m_pNet->sendData(socketWaiter,(char*)&surs,sizeof(surs));
}

void kernel::UploadFileContentRq(SOCKET socketWaiter, const char *szbuf)
{
    STRU_UPLOADFILECONTENT_RQ *psus = (STRU_UPLOADFILECONTENT_RQ*)szbuf;
    //接受到的某一个文件的内容 找到文件
    //遍历链表找到文件
    fileinfo *p = nullptr;
    auto ite = m_lstFileInfo.begin();
    while(ite!=m_lstFileInfo.end()){
        if((*ite)->fileId == psus->fileid && (*ite)->userId == psus->userid) {
            //找到了当前文件内容的归属
            p = *ite;
            break;
        }
        ite++;
    }

    //根据文件指针写入内容
    size_t WriteNum = fwrite(psus->m_FileContent,sizeof(char),psus->m_fileNum,p->pFile);
    if(WriteNum > 0 ){
        p->pos += WriteNum;
        if(p->pos == p->fileSize) {
            //当前文件的内容书写完毕
            fclose(p->pFile);
            delete p;
            m_lstFileInfo.erase(ite);
        }
    }
}

void kernel::SelectFileRq(SOCKET socketWaiter, const char *szbuf)
{
    STRU_SELECTFILE_RQ* ssr = (STRU_SELECTFILE_RQ*)szbuf;
    STRU_SELECTFILE_RS ssrs;
    char szsql[SQLLEN] = {0};
    int index = 0;
    list<string> lsStr;
    //看是那个人获取文件信息
    sprintf(szsql,"select f_name,f_size,f_uploadtime from ufile where u_id = %lld and f_name like '%%%s%%';",ssr->userid,ssr->m_KeyWord);
    //数据查询此人上传过的文件
    m_pSql->SelectMysql(szsql,3,lsStr);
    //封装进GetFileListRS结构体中
    while(lsStr.size()>0){
        string fileName = lsStr.front();
        lsStr.pop_front();

        string fileSize = lsStr.front();
        lsStr.pop_front();

        string fileTime = lsStr.front();
        lsStr.pop_front();

        strcpy_s(ssrs.arrFileInfo[index].szFileName,fileName.c_str());
        strcpy_s(ssrs.arrFileInfo[index].szFileUploadTime,fileTime.c_str());
        ssrs.arrFileInfo[index].szFileSize = atoll(fileSize.c_str());
        index++;
        if(index == FILE_NUM||lsStr.size() == 0){ //文件信息数组中已经包含15个文件 链表所有数据已经取出来了
            ssrs.szFileNum = index;
            //返还给客户端
            m_pNet->sendData(socketWaiter,(char*)&ssrs,sizeof(ssrs));
            ZeroMemory(ssrs.arrFileInfo,sizeof(ssrs.arrFileInfo));  //内存清空
            index = 0;
        }
    }
    //返还给客户端
}

void kernel::DeleteFileRq(SOCKET socketWaiter, const char *szbuf)
{
    STRU_DELETEFILE_RQ* psdr = (STRU_DELETEFILE_RQ*)szbuf;
    char szsql[SQLLEN] = {0};
    list<string> lsStr;
    sprintf(szsql, "select f_id, f_count, f_path from ufile where u_id = %lld and f_name = '%s'", psdr->userId, psdr->szFileName);
    m_pSql->SelectMysql(szsql, 3, lsStr);
    if(lsStr.size() > 0){
        string strFileId = lsStr.front();
        lsStr.pop_front();

        string strFileCount = lsStr.front();
        lsStr.pop_front();

        string strFilePath = lsStr.front();
        lsStr.pop_front();

        long long fileId = atoll(strFileId.c_str());
        long long fileCount = atoi(strFileCount.c_str());

        //删除映射信息
        sprintf(szsql, "delete from user_file where u_id = %lld and f_id = %lld", psdr->userId,fileId);
        m_pSql->UpdateMysql(szsql);

        if (fileCount > 1) {
            //修改引用计数
            sprintf(szsql, "update file set f_count = f_count - 1 where f_id = %lld", fileId);
            m_pSql->UpdateMysql(szsql);
        } else {
            //删除文件
            sprintf(szsql, "delete from file where f_id = %lld", fileId);
            m_pSql->UpdateMysql(szsql);
            DeleteFileA(strFilePath.c_str());
        }
    }
}

void kernel::ShareLinkRq(SOCKET socketWaiter, const char *szbuf)
{
    STRU_SHARELINK_RQ* pssr = (STRU_SHARELINK_RQ*)szbuf;
    STRU_SHARELINK_RS sss;
    char szsql[SQLLEN] = {0};
    list<string> lsStr;
    strcpy_s(sss.szFileName, pssr->szFileName);

    //随机数
    srand(time(NULL));
    char szcode[MAX_SIZE];
    char c;
    for(int i=0;i<4;i++){
        int num = rand() % 36;
        if(num < 10)
            c = num + '0';
        else
            c = num - 10 + 'A';
        szcode[i] = c;
    }

    strcpy_s(sss.szCode, szcode);
    //user_shared表插入数据
    sprintf(szsql, "select f_id from ufile where u_id = %lld and f_name = '%s';",pssr->userId,pssr->szFileName);
    m_pSql->SelectMysql(szsql, 1, lsStr);
    if(lsStr.size() > 0){
        string fileId = lsStr.front();
        lsStr.pop_front();

        sprintf(szsql,"insert into user_shared(uid,fid,code) values(%lld,%lld,'%s');",pssr->userId,atoll(fileId.c_str()),szcode);

        if(m_pSql->UpdateMysql(szsql)) {
            strcpy_s(sss.szCode,szcode);
        }else{
            sprintf(szsql,"select code from user_shared where uid = %lld and fid = %lld; ",pssr->userId,atoll(fileId.c_str()));
            m_pSql->SelectMysql(szsql,1,lsStr);
            string code = lsStr.front();
            lsStr.pop_front();
            strcpy_s(sss.szCode,code.c_str());
        }
    }
    //返还给客户端一个回复
    m_pNet->sendData(socketWaiter, (char*)&sss, sizeof(sss));
}

void kernel::GetLinkRq(SOCKET socketWaiter, const char *szbuf)
{
    STRU_GETLINK_RQ *psgq = (STRU_GETLINK_RQ*)szbuf;
    STRU_GETLINK_RS sgs;
    char szsql[SQLLEN] = {0};
    list<string> lsStr;

    //code 码值查询数据
    sprintf(szsql, "select uid, fid from user_shared where code = '%s';", psgq->szCode);
    m_pSql->SelectMysql(szsql, 2, lsStr);

    if(lsStr.size() > 0) {
        //判断uid
        string uid = lsStr.front();
        lsStr.pop_front();

        string fid = lsStr.front();
        lsStr.pop_front();

        if(atoll(uid.c_str()) == psgq->userId) {
            sgs.szResult = _getlink_failed;
        }else{
            sgs.szResult = _getlink_success;
            //查询文件信息
            sprintf(szsql,"select f_name,f_size from ufile where f_id = %lld;", atoll(fid.c_str()));
            m_pSql->SelectMysql(szsql,2,lsStr);

            if(lsStr.size()>0){
                strcpy_s(sgs.szFileName,lsStr.front().c_str());
                lsStr.pop_front();

                sgs.szFileSize = atoll(lsStr.front().c_str());
                lsStr.pop_front();

                strcpy_s(sgs.szFileUploadTime, sgs.szFileUploadTime);
                lsStr.pop_front();
            }

            //修改文件的引用计数，向用户与文件映射表插入一条数据
            sprintf(szsql, "insert into user_file(f_id, u_id, time) values(%lld, %lld, '%s');", atoll(fid.c_str()),psgq->userId,psgq->szFileUploadTime);
            if(m_pSql->UpdateMysql(szsql)){
                sprintf(szsql, "update file set f_count = f_count + 1 where f_id = %lld", atoll(fid.c_str()));
                m_pSql->UpdateMysql(szsql);
            }
        }
    }

    //返还给客户端
    m_pNet->sendData(socketWaiter, (char*)&sgs, sizeof(sgs));
}

void kernel::DownLoadFileRq(SOCKET socketWaiter, const char *szbuf)
{
    STRU_DOWNLOADFILE_RQ *psdq = (STRU_DOWNLOADFILE_RQ*)szbuf;
    STRU_DOWNLOADFILE_RS sds;
    char szsql[SQLLEN] = {0};
    list<string> lsStr;
    sprintf(szsql,"select f_path from ufile where u_id = %lld and f_name = '%s'", psdq->userId, psdq->szFileName);
    m_pSql->SelectMysql(szsql,1,lsStr);
    if(lsStr.size() > 0) {
        //向客户端返还文件内容
        string filePath = lsStr.front();
        lsStr.pop_front();
        FILE* pFile = fopen(filePath.c_str(),"rb");
        int num;
        while((num = fread(sds.m_FileContent,1,ONE_PAGE,pFile))>0){
            sds.m_fileNum = num;
            m_pNet->sendData(socketWaiter,(char*)&sds,sizeof(sds));
        }
        fclose(pFile);
    }
}