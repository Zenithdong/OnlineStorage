#include "cmysql.h"
#include <iostream>

namespace {
/// 连接/读写超时秒数：防止数据库不可达时服务端线程永久阻塞。
constexpr unsigned int TIMEOUT_SECONDS = 5;

/**
 * @brief 为新建的 MYSQL 句柄统一配置超时与字符集。
 * @param connection 句柄。
 * @note mysql_options 返回非零表示选项设置失败（errno 不可用，MySQL C API 以返回码表示错误）。
 *       utf8mb4 保证中文文件名与用户名可正确转义/存储。
 */
void ConfigureConnection(MYSQL* connection) {
    if (!connection) {
        return;
    }

    // 有限超时防止数据库不可达时服务端线程永久阻塞；utf8mb4 保证中文文件名和用户名可正确转义。
    unsigned int timeoutSeconds = TIMEOUT_SECONDS;
    mysql_options(connection, MYSQL_OPT_CONNECT_TIMEOUT, &timeoutSeconds);
    mysql_options(connection, MYSQL_OPT_READ_TIMEOUT, &timeoutSeconds);
    mysql_options(connection, MYSQL_OPT_WRITE_TIMEOUT, &timeoutSeconds);
    mysql_options(connection, MYSQL_SET_CHARSET_NAME, "utf8mb4");
}
} // namespace

/**
 * @brief 初始化连接句柄并配置选项。
 * @note mysql_init 失败返回 nullptr，此时 unique_ptr 持空、后续操作静默失败；
 *       deleter 显式传入 &mysql_close，使句柄析构/重连时正确释放。
 */
CMySql::CMySql() : mysql(mysql_init(nullptr), &mysql_close) {
    ConfigureConnection(mysql.get());
}

/**
 * @brief 断开连接（unique_ptr 析构调用 mysql_close）。
 */
CMySql::~CMySql() {
    DisConnect();
}

/**
 * @brief 连接数据库。
 * @return 是否成功。
 * @note mysql_real_connect 失败返回 nullptr，错误信息用 mysql_error() 获取（非 errno）。
 *       断开后句柄被清空，重连路径重新 mysql_init 并恢复连接选项。
 */
bool CMySql::ConnectMySql(const char* ip, const char* user, const char* password, const char* db) {
    // 断开后句柄会被清空，重连路径需要重新创建句柄并恢复连接选项。
    if (!mysql) {
        mysql.reset(mysql_init(nullptr));
        if (!mysql) {
            return false;
        }
        ConfigureConnection(mysql.get());
    }

    if (!mysql_real_connect(mysql.get(), ip, user, password, db, 0, nullptr, 0)) {
        std::cerr << mysql_error(mysql.get()) << std::endl;
        return false;
    }
    return true;
}

/**
 * @brief 关闭连接并清空句柄。
 */
void CMySql::DisConnect() {
    mysql.reset();
}

/**
 * @brief 执行查询并返回结果集。
 * @return 每行一个 vector<string>；失败/空返回空 vector。
 * @note 内存管理：mysql_store_result 把完整结果缓存到客户端堆内存，
 *       业务层在释放结果集前已复制出字符串副本；读完必须 mysql_free_result 释放，
 *       否则每次查询泄漏一个结果集。
 */
std::vector<std::vector<std::string>> CMySql::SelectMysql(const std::string& sql, int nColumn) {
    std::vector<std::vector<std::string>> rows;
    // 查询接口只接受预期列数大于零的结果集，避免业务层按错误步长解释结果。
    if (!mysql || nColumn <= 0) {
        return rows;
    }
    if (mysql_query(mysql.get(), sql.c_str())) {
        std::cerr << mysql_error(mysql.get()) << std::endl;
        return rows;
    }

    // mysql_store_result 把完整结果缓存在客户端内存中，便于业务层在释放结果集后继续使用字符串副本。
    MYSQL_RES* queryResult = mysql_store_result(mysql.get());
    if (!queryResult) {
        std::cerr << mysql_error(mysql.get()) << " 查询没有返回结果集" << std::endl;
        return rows;
    }

    if (static_cast<unsigned int>(nColumn) > mysql_num_fields(queryResult)) {
        mysql_free_result(queryResult);
        return rows;
    }

    // SQL NULL 被统一保存为文本 "null"；每行作为一个 vector<string> 追加，业务层按 [行][列] 读取。
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(queryResult)) != nullptr) {
        std::vector<std::string> rowValues;
        rowValues.reserve(nColumn);
        for (int i = 0; i < nColumn; i++) {
            rowValues.push_back(row[i] ? row[i] : "null");
        }
        rows.push_back(std::move(rowValues));
    }

    mysql_free_result(queryResult);
    return rows;
}

/**
 * @brief 执行更新/事务语句。
 * @return 是否成功。
 * @note mysql_query 返回非零表示失败（错误用 mysql_error() 获取）。
 */
bool CMySql::UpdateMysql(const std::string& sql) {
    // 更新接口也用于 start transaction、commit 和 rollback，事务边界由具体业务函数编排。
    if (!mysql) {
        return false;
    }
    if (mysql_query(mysql.get(), sql.c_str())) {
        std::cerr << mysql_error(mysql.get()) << std::endl;
        return false;
    }
    return true;
}

/**
 * @brief 转义外部文本。
 * @return 转义后字符串。
 * @note mysql_real_escape_string 需按当前字符集转义；最坏情况每输入字节转义为两字节，
 *       故缓冲预留 2×长度+1，转义后 resize 到实际长度。
 */
std::string CMySql::EscapeString(std::string_view value) const {
    if (!mysql || value.empty()) {
        return "";
    }

    // 最坏情况下每个输入字节都需要转义为两个字节，再额外预留结尾空间。
    unsigned long inputLength = static_cast<unsigned long>(value.size());
    std::string escaped(inputLength * 2 + 1, '\0');
    unsigned long escapedLength =
        mysql_real_escape_string(mysql.get(), escaped.data(), value.data(), inputLength);
    escaped.resize(escapedLength);
    return escaped;
}

/**
 * @brief 返回最近一次插入的自增主键。
 */
unsigned long long CMySql::LastInsertId() const {
    return mysql ? mysql_insert_id(mysql.get()) : 0;
}
