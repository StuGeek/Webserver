#include "Mysql_connpool.h"
#include <stdio.h>

Mysql_Connpool::Mysql_Connpool():use_conn_num(0), free_conn_num(0) {}

Mysql_Connpool::~Mysql_Connpool() {
    pthread_mutex_lock(&mutex_conn_que);

    // 对连接池上锁后，关闭其中的所有连接，再释放锁
    while (!conn_que.empty()) {
        MYSQL *sql = conn_que.front();
        conn_que.pop();
        mysql_close(sql);
    }
    use_conn_num = 0;
    free_conn_num = 0;

    pthread_mutex_unlock(&mutex_conn_que);
}

// 初始化数据库连接池
void Mysql_Connpool::init_mysql_connpool(const char *host, const char *user, 
    const char *passwd, const char *db_name, int port, int conn_num)
{
    this->host = host;
    this->user = user;
    this->passwd = passwd;
    this->db_name = db_name;
    this->port = port;
    // 连接池中的最大连接个数为设定的连接个数
    max_conn_num = conn_num;

    // 创建指定个数的连接
    for (int i = 0; i < max_conn_num; ++i) {
        MYSQL *sql = NULL;
        sql = mysql_init(sql);
        if (!sql) {
            printf("mysql_init error!\n");
        }

        sql = mysql_real_connect(sql, host, user, passwd, db_name, port, NULL, 0);
        if (!sql) {
            printf("mysql_real_connect error!\n");
        }

        conn_que.push(sql);
    }

    free_conn_num = max_conn_num;
    sem_init(&sem_free_conn, 0, max_conn_num);
}

// 从连接池中获取空闲连接
MYSQL *Mysql_Connpool::get_mysql_conn() {
    MYSQL *sql = NULL;
    if (conn_que.size() == 0) {
        printf("mysql_connpool busy!\n");
        return NULL;
    }

    sem_wait(&sem_free_conn);
    pthread_mutex_lock(&mutex_conn_que);

    sql = conn_que.front();
    conn_que.pop();

    use_conn_num++;
    free_conn_num--;

    pthread_mutex_unlock(&mutex_conn_que);

    return sql;
}

// 释放连接到连接池中
bool Mysql_Connpool::release_mysql_conn(MYSQL *sql) {
	if (!sql) {
        return false;
    }

    pthread_mutex_lock(&mutex_conn_que);

    conn_que.push(sql);

    use_conn_num--;
    free_conn_num++;

    pthread_mutex_unlock(&mutex_conn_que);
    sem_post(&sem_free_conn);

    return true;
}

// 获取空闲的连接个数
int Mysql_Connpool::get_free_mysql_conn_num() {
    pthread_mutex_lock(&mutex_conn_que);

    int free_conn = free_conn_num;

    pthread_mutex_unlock(&mutex_conn_que);

	return free_conn;
}

// 获取数据库连接池实例
Mysql_Connpool* Mysql_Connpool::get_instance() {
    static Mysql_Connpool connpool;
    return &connpool;
}

// 获取连接池资源时初始化的构造函数
Mysql_conn_RAII::Mysql_conn_RAII(MYSQL **sql, Mysql_Connpool *connpool) {
	if (!connpool) {
        printf("connection pool is not existed!\n");
        return;
    }
    
    // 从指定连接池中获取空闲连接
    *sql = connpool->get_mysql_conn();
    sql_RAII = *sql;
    connpool_RAII = connpool;
}

// 连接池对象析构时释放资源的析构函数
Mysql_conn_RAII::~Mysql_conn_RAII() {
    // 从当前使用连接池中释放连接
    if (sql_RAII) {
        connpool_RAII->release_mysql_conn(sql_RAII);
    }
}