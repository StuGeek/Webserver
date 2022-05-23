#ifndef _WEBSERVER_H_
#define _WEBSERVER_H_

#include "http_process.h"
#include "ThreadPool.h"

#define MAX_FDS_NUM 50000     // 最大的文件描述符个数
#define MAX_EVENTS_NUM 30000  // 监听的最大的事件数量

// 向epoll里添加监听的文件描述符，并设置为非阻塞
extern void epoll_add_fd(int epoll_fd, int listen_fd, bool is_oneshot);

class Webserver {
public:
    static const int DEFAULT_PORT = 8000;  // 默认端口号
    const int EPOLL_SIZE = 30000;          // epoll监听的fd个数

public:
    Webserver(int port = DEFAULT_PORT, bool is_log_open = true, bool is_async_write = false);
    ~Webserver();

    void start_server();                   // 启动服务器

private:
    int port;                              // 服务器端口
    ThreadPool<http_process> *threadpool;  // 处理http请求的线程池
    http_process *requests;                // 存储http请求的数组
    Mysql_Connpool *connpool;              // mysql数据库连接池

    int epoll_fd;                          // epoll句柄
    int server_socket_fd;                  // 服务器的套接字

    void initmysql_result();               // 初始化数据库，将相应用户信息结果放入内存
};

#endif