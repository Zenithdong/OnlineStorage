#ifndef CMYSQL_H
#define CMYSQL_H
#include <mysql.h>
#include <QDebug>
#include <list>
#include <string>
using namespace std;
class CMySql
{

public:
    CMySql();
    ~CMySql();
public:
    bool ConnectMySql(const char* ip,const char* user,const char* password,const char* db);
    void DisConnect();
    bool SelectMysql(const char* sql,int nColumn,list<string>& lst);      //select
    bool UpdateMysql(const char* sql);     //执行的就是insert update  delete
private:
    MYSQL* mysql;
    MYSQL_RES* result;
    MYSQL_ROW row;
};
/*
    连接数据库
    拼写sql语句
    sql语句交给MYSQL 执行
    处理结果  select       update delete insert
    关闭连接

    用的是 MYSQL 中的API
*/
#endif // CMYSQL_H
