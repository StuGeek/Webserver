#ifndef _MYSQL_CONNPOOL_H_
#define _MYSQL_CONNPOOL_H_

#include <mysql/mysql.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <queue>
#include <unordered_map>

using namespace std;

// mysql数据库连接池
class Mysql_Connpool {
public:
    Mysql_Connpool();
    ~Mysql_Connpool();

    MYSQL *get_mysql_conn();                // 从连接池中获取空闲连接
    bool release_mysql_conn(MYSQL *conn);   // 释放连接到连接池中
    int get_free_mysql_conn_num();          // 获取空闲的连接个数

    static Mysql_Connpool *get_instance();  // 获取数据库连接池实例

    // 初始化数据库连接池
    void init_mysql_connpool(const char *host, const char *user, 
                             const char *passwd, const char *db_name,
                             int port, int conn_num);

    unordered_map<string, string> user_passwd;  // 存储从数据库中导出的用户名和对应的密码

private:
    const char *host;     // 数据库主机
    const char *user;     // 数据库用户名
    const char *passwd;   // 数据库密码
    const char *db_name;  // 数据库名称
    int port;             // 数据库端口

    int max_conn_num;     // 连接池中的最大连接个数
    int use_conn_num;     // 正在使用的连接个数
    int free_conn_num;    // 空闲的连接个数

    pthread_mutex_t mutex_conn_que;  // 数据库连接池操作锁
    sem_t sem_free_conn;             // 表示数据库空闲连接数的信号量

    queue<MYSQL *> conn_que;         // 数据库连接池
};

// mysql数据库连接池的管理资源类
class Mysql_conn_RAII {
public:
    // 获取连接池资源时初始化的构造函数
    Mysql_conn_RAII(MYSQL **sql, Mysql_Connpool *connpool);
    // 连接池对象析构时释放资源的析构函数
    ~Mysql_conn_RAII();

private:
    MYSQL *sql_RAII;                // 指向要使用的数据库连接的指针
    Mysql_Connpool *connpool_RAII;  // 指向要使用的数据库连接池
};

#endif