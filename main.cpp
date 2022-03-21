#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include "ThreadPool.h"
#include "http_process.h"

#define MAX_FDS_NUM 50000     // 最大的文件描述符个数
#define MAX_EVENTS_NUM 20000  // 监听的最大的事件数量

const int default_port_number = 8000;  // 默认端口号
const int epoll_size = 5;              // epoll监听的fd个数

// 向epoll里添加监听的文件描述符，并设置为非阻塞
extern void epoll_add_fd(int epoll_fd, int listen_fd, bool is_oneshot);

int main(int argc, char *argv[]) {
    // 记录端口号
    int port_number = default_port_number;
    if (argc > 1) {
        port_number = atoi(argv[1]);
    }
    printf("server port: %d\n", port_number);

    int ret = 0;
    
    // 创建处理http请求的线程池
    ThreadPool<http_process> *threadpool = NULL;
    try {
        threadpool = new ThreadPool<http_process>;
    } catch(const std::bad_alloc &e) {
        printf("new ThreadPool error!\n");
        exit(1);
    }

    // 创建存储http请求的数组
    http_process *requests = NULL;
    try {
        requests = new http_process[MAX_FDS_NUM];
    } catch(const std::bad_alloc &e) {
        printf("new http_process error!\n");
        exit(1);
    }

    // 创建服务器的socket
    int server_socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(server_socket_fd < 0) { 
        printf("server_socket_fd socket() error!\n"); 
        exit(1); 
    }
    // 对处于TIME_WAIT状态下的socket，设置端口复用
    int opt = 1; 
    ret = setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); 
    if (ret < 0) {
        printf("setsockopt error!\n");
        exit(1);
    }

    // 设置服务端套接字的信息
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); 
    // 设置服务器的IP协议为IPv4
    server_addr.sin_family = AF_INET;
    // 设置服务端IP地址
    server_addr.sin_addr.s_addr = inet_addr("0.0.0.0");
    // 设置服务器端口号
    server_addr.sin_port = htons(port_number);

    // 将地址与套接字进行绑定
	ret = bind(server_socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if(ret < 0) {
		printf("bind() error!\n");
		close(server_socket_fd);
		exit(1); 
	}
	printf("server bind successful!\n");

	// 服务器端监听，服务器的套接字排队的最大连接个数为epoll监听的fd个数
	ret = listen(server_socket_fd, epoll_size);
    if (ret != 0) {
		printf("Server listen error!\n");
		exit(1);
	}
  	printf("listening...\n\n");

    // 创建epoll事件数组和epoll对象
    epoll_event events[MAX_EVENTS_NUM];
    // 创建一个epoll句柄，记录这个epoll句柄的fd
    int epoll_fd = epoll_create(epoll_size);
    // 每个http请求都会被添加到这个epoll句柄中
    http_process::epoll_fd = epoll_fd;
    // 将服务器的fd添加到epoll对象中，注册读就绪事件
    epoll_add_fd(epoll_fd, server_socket_fd, false);

    while (true) {
        // 获取epoll句柄需要处理的事件数目
        int event_num = epoll_wait(epoll_fd, events, MAX_EVENTS_NUM, -1);
        // 如果调用失败且不是信号中断导致默认阻塞的epoll_wait方法返回-1，那么退出
        if (event_num < 0 && errno != EINTR) {
            printf("epoll_wait error!\n");
            break;
        }

        // 遍历要处理事件
        for (int i = 0; i < event_num; ++i) {
            // 获取epoll中的监听的事件和事件的fd
            int event_fd = events[i].data.fd;
            auto event = events[i].events;
            // 如果服务器套接字监听到有事件，说明客户端有http请求连接
            if (event_fd == server_socket_fd) {
                // 设置连接客户端的套接字的信息
                struct sockaddr_in client_addr;
                memset(&client_addr, 0, sizeof(client_addr));
                socklen_t client_addr_len = sizeof(client_addr);
                // 返回与客户端进行连接通信的套接字的fd
                int request_socket_fd = accept(server_socket_fd, (struct sockaddr *)&client_addr, &client_addr_len);
                if (request_socket_fd < 0) {
                    printf("request_socket_fd accept() error!\n");
                    continue;
                }

                // 如果客户端的连接数已经满了，服务器正在忙
                if (http_process::client_cnt >= MAX_FDS_NUM) {
                    // 关闭与客户端连接的套接字
                    close(request_socket_fd);
                    continue;
                }

                // 初始化连接的客户端的数据
                requests[request_socket_fd].init_process(request_socket_fd, client_addr);
            }
            // 如果客户端发生了异常错误或断开，那么结束http处理过程
            else if (event & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                requests[event_fd].end_process();
            }
            // 如果发生了读事件
            else if (event & EPOLLIN) {
                // 判断数据能否一次性全部读完
                if (!requests[event_fd].read()) {
                    // 不能则结束http处理过程
                    requests[event_fd].end_process();
                } else {
                    // 能则将请求事件添加到线程池中执行
                    threadpool->add_request(&requests[event_fd]);
                }
            }
            // 如果发生了写事件
            else if (event & EPOLLOUT) {
                // 判断能否一次性写完数据
                if (!requests[event_fd].write()) {
                    // 不能则结束http处理过程
                    requests[event_fd].end_process();
                }
            }
        }
    }

    // 关闭epoll句柄和服务器的套接字
    close(epoll_fd);
    close(server_socket_fd);
    printf("server close!\n");
    
    delete [] requests;
    delete threadpool;
}