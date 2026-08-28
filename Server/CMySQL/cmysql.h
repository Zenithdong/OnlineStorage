#ifndef CMYSQL_H
#define CMYSQL_H
#include <mysql/mysql.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file cmysql.h
 * @brief MySQL C API 封装：连接、查询、更新、转义与自增主键。
 *
 * 所属模块：服务端数据库层（CMySQL）。
 * 依赖系统库：libmysqlclient（<mysql/mysql.h>，链接 -lmysqlclient 及 z/ssl/crypto 等）。
 *
 * 并发模型：单 MYSQL 连接仅被 Reactor 线程使用（单线程无锁）。
 * 连接句柄用 unique_ptr<MYSQL, decltype(&mysql_close)> 做 RAII——构造函数显式传 deleter，
 * 因此无需默认构造；析构/重连时自动 mysql_close，避免句柄泄漏。
 */
class CMySql {
public:
    /// @brief 初始化连接句柄并配置超时与字符集。
    CMySql();

    /// @brief 断开连接（RAII：unique_ptr 析构调用 mysql_close）。
    ~CMySql();

public:
    /**
     * @brief 使用给定账号连接数据库。
     * @param ip 服务器地址。
     * @param user 用户名。
     * @param password 密码。
     * @param db 数据库名。
     * @return 是否连接成功。
     * @note 断开后句柄被清空，重连路径会重新 mysql_init 并恢复连接选项。
     */
    bool ConnectMySql(const char* ip, const char* user, const char* password, const char* db);

    /// @brief 关闭当前连接并清空句柄，可安全重复调用。
    void DisConnect();

    /**
     * @brief 执行返回结果集的 SQL。
     * @param sql 查询语句。
     * @param nColumn 每行期望读取的列数。
     * @return 每行一个 vector<string>（nColumn 个字段）；失败或无结果返回空 vector。
     * @note 结果集由 mysql_store_result 缓存在客户端内存，读取后立即 free_result；
     *       SQL NULL 统一存为文本 "null"。
     */
    std::vector<std::vector<std::string>> SelectMysql(const std::string& sql, int nColumn);

    /**
     * @brief 执行 INSERT/UPDATE/DELETE 与事务控制语句。
     * @param sql 语句。
     * @return 是否执行成功。
     * @note 也用于 start transaction / commit / rollback，事务边界由业务函数编排。
     */
    bool UpdateMysql(const std::string& sql);

    /**
     * @brief 使用当前连接字符集转义外部文本。
     * @param value 待转义文本。
     * @return 转义后的字符串（空输入返回空串）。
     * @note 防 SQL 注入：所有外部输入拼入 SQL 前必须经此转义。
     */
    std::string EscapeString(std::string_view value) const;

    /**
     * @brief 返回最近一次成功插入的自增主键。
     * @return 自增 ID；无连接返回 0。
     */
    unsigned long long LastInsertId() const;

private:
    std::unique_ptr<MYSQL, decltype(&mysql_close)> mysql; ///< MySQL 连接句柄（RAII）。
};
#endif
