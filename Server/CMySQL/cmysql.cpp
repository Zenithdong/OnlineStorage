#include "cmysql.h"

CMySql::CMySql()
{
    mysql = new MYSQL;
    mysql_init(mysql);
    mysql_set_character_set(mysql, "utf8");
}

CMySql::~CMySql()
{
    delete mysql;
    mysql_free_result(result);
}

bool CMySql::ConnectMySql(const char* ip, const char* user, const char* password, const char* db)
{
    if (NULL == mysql_real_connect(mysql, ip, user, password, db, 0, NULL, 0))
        return false;
    return true;
}

void CMySql::DisConnect()
{
    mysql_close(mysql);
}

bool CMySql::SelectMysql(const char* sql, int nColumn, std::list<std::string>& lst)
{
    if (sql == NULL)
        return false;

    if (mysql_query(mysql, sql)) {
        std::cerr << mysql_error(mysql) << std::endl;
        return false;
    }

    result = mysql_store_result(mysql);
    if (result == NULL) {
        std::cerr << mysql_error(mysql) << " 没查到结果" << std::endl;
        return false;
    }
    else {
        while ((row = mysql_fetch_row(result)) != NULL) {
            for (int i = 0; i < nColumn; i++) {
                if (row[i] == NULL) {
                    row[i] = (char*)"null";
                }
                lst.push_back(row[i]);
            }
        }
    }
    return true;
}

bool CMySql::UpdateMysql(const char* sql)
{
    if (sql == NULL)
        return false;

    if (mysql_query(mysql, sql)) {
        std::cerr << mysql_error(mysql) << std::endl;
        return false;
    }
    return true;
}
