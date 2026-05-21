#include "cmysql.h"

CMySql::CMySql() {
    //初始化
    mysql = new MYSQL;
    //mysql_init(MYSQL* mysql)
    mysql_init(mysql);

    //解决中文乱码  设置字符集
    mysql_set_character_set(mysql,"utf8");
}

CMySql::~CMySql()
{
    delete mysql;
    mysql_free_result(result);
}

bool CMySql::ConnectMySql(const char* ip,const char* user,const char* password,const char* db)
{
    //mysql_real_connect()
    //参数
    //MYSQL* mysql 变量
    //const char* host  服务器的地址
    //const char* user  用户
    //const char* password 密码
    //const char* db   数据库
    //unsigned int port  端口号   0根据ip地址选取端口号
    //NULL
    //0
    if(NULL == mysql_real_connect(mysql,ip,user,password,db,0,NULL,0))    //连接数据库
        return false;
    return true;
}

void CMySql::DisConnect()
{
    //mysql_close()
    mysql_close(mysql);
}

bool CMySql::SelectMysql(const char* sql, int nColumn, list<string>& lst)
{
    if(sql == NULL)
        return false;
    //mysql_query()    返回值受影响的行数
    //MYSQL* mysql   对象
    //const char* query   需要执行的SQL语句
    if(mysql_query(mysql,sql)){
        qDebug()<<mysql_error(mysql);
        return false;
    }
    //MYSQL_RES* mysql_store_result(MYSQL*)   获取查询到的结果    存到一个MYSQL_RES  对象中
    //参数 哪一个mysql对象
    //返回值  取到得表格   //MYSQL_RES 对象  就代表查到的表格
    result = mysql_store_result(mysql);
    if(result == NULL){
        qDebug()<<mysql_error(mysql)<<"没查到结果";
        return false;
    }else{
        //mysql_fetch_row();          //按行拿取数据    返回值是
        while((row = mysql_fetch_row(result)) !=NULL){
            for(int i=0;i < nColumn;i++){
                if(row[i] == NULL){
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
    if(sql == NULL)
        return false;
    if(mysql_query(mysql,sql)){
        qDebug()<<mysql_error(mysql);
        return false;
    }
    return true;
}
