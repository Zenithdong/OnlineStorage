#ifndef CMYSQL_H
#define CMYSQL_H

#include <mysql.h>
#include <iostream>
#include <list>
#include <string>

class CMySql
{
public:
    CMySql();
    ~CMySql();

public:
    bool ConnectMySql(const char* ip, const char* user, const char* password, const char* db);
    void DisConnect();
    bool SelectMysql(const char* sql, int nColumn, std::list<std::string>& lst);
    bool UpdateMysql(const char* sql);

private:
    MYSQL* mysql;
    MYSQL_RES* result;
    MYSQL_ROW row;
};

#endif // CMYSQL_H
