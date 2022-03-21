# 简单Webserver实现

## 项目基本信息

### 项目环境

+ 架构：Intel x86_64 (虚拟机)
+ 操作系统：Ubuntu 20.04
+ 汇编器：gas (GNU Assembler) in AT&T mode
+ 编译器：gcc

### 文件夹结构

```
Webserver
|-- resources
    |-- imgs
    |-- index.html
|-- webbench
    |-- webbench
|-- http_process.cpp
|-- http_process.h
|-- main.cpp
|-- ThreadPool.h
|-- README.md
```

### 运行方式

进入Webserver文件夹目录，在终端输入：

```
g++ *.cpp -pthread
./a.out
```

服务器默认在8000端口运行，如果想换其它端口，则使用命令`./a.out port_number`之后，即可在端口号为`port_number`的端口运行服务器。

### 压力测试

服务器运行之后，另外开启一个终端，进入`Webserver/webbench`目录，输入：

```
./webbench -c 并发数 -t 运行测试时间 测试网站URL 
```

即可对服务端进行压力测试。

### 实现功能

+ 游戏设计要求：
  + 创建一个地图和若干巡逻兵(使用动画)；
  + 每个巡逻兵走一个3~5个边的凸多边型，位置数据是相对地址。即每次确定下一个目标位置，用自己当前位置为原点计算；
  + 巡逻兵碰撞到障碍物，则会自动选下一个点为目标；
  + 巡逻兵在设定范围内感知到玩家，会自动追击玩家；
  + 失去玩家目标后，继续巡逻；
  + 计分：玩家每次甩掉一个巡逻兵计一分，与巡逻兵碰撞游戏结束；
+ 程序设计要求：
  + 必须使用订阅与发布模式传消息
  + 工厂模式生产巡逻兵
+ 友善提示1：生成 3~5个边的凸多边型
  + 随机生成矩形
  + 在矩形每个边上随机找点，可得到 3 - 4 的凸多边型
  + 5 ?
+ 友善提示2：参考以前博客，给出自己新玩法

### 项目实现过程

#### 1. 服务器编程基本框架

![](./imgs/1.png)

+ I/O 处理单元是服务器管理客户连接的模块。它通常要完成以下工作：等待并接受新的客户连接，接收客户数据，将服务器响应数据返回给客户端。但是数据的收发不一定在 I/O 处理单元中执行，也可能在逻辑单元中执行，具体在何处执行取决于事件处理模式。
+ 一个逻辑单元通常是一个进程或线程。它分析并处理客户数据，然后将结果传递给 I/O 处理单元或者直接发送给客户端（具体使用哪种方式取决于事件处理模式）。服务器通常拥有多个逻辑单元，以实现对多个客户任务的并发处理。
+ 网络存储单元可以是数据库、缓存和文件，但不是必须的。
+ 请求队列是各单元之间的通信方式的抽象。I/O 处理单元接收到客户请求时，需要以某种方式通知一个逻辑单元来处理该请求。同样，多个逻辑单元同时访问一个存储单元时，也需要采用某种机制来协调处理竞态条件。请求队列通常被实现为池的一部分。

#### 2. 实现请求队列

在服务器编程基本框架的四个组成部分中，首先实现请求队列部分，具体思路为创建一个处理http请求的线程池类，这个线程池类中包含请求队列，和添加任务到请求队列的方法，线程池中的线程从请求队列中取任务来执行，这个部分在`ThreadPool.h`中实现：

```C++
#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <list>

// 线程池类，将它定义为模板类是为了代码复用，模板参数T是任务类
template<typename T>
class ThreadPool {
public:
    // 线程池中的线程数默认为4，请求队列中的最大请求数默认为50000
    ThreadPool(int threads_num = 4, int requests_max_num = 50000);
    ~ThreadPool();
    bool add_request(T *request);          // 向请求队列添加请求

private:
    static void *request_func(void *arg);  // 每个任务请求执行的函数

private:
    int threads_num;                       // 线程数量
    int requests_max_num;                  // 请求队列中的最大请求数
    pthread_t *threads;                    // 线程池的数组
    std::list<T *> request_queue;          // 请求队列
    bool is_end;                           // 是否结束线程池
    sem_t sem_add_request;                 // 添加请求时用到的信号量
    sem_t sem_request_num;                 // 表示请求队列中请求数的信号量               
};

template <typename T>
ThreadPool<T>::ThreadPool(int threads_num, int requests_max_num) :
        threads_num(threads_num), requests_max_num(requests_max_num),
        threads(NULL), is_end(false) {

    int ret = 0;

    // 线程池中的线程数要大于0
    if (threads_num <= 0) {
        printf("The number of threads should be more than zero!");
        exit(1);
    }

    // 请求队列中的最大请求数要大于0
    if (requests_max_num <= 0) {
        printf("The max number of requests should be more than zero!");
        exit(1);
    }

    // 创建线程数组
    try {
        threads = new pthread_t[threads_num];
    } catch(const std::bad_alloc &e) {
        printf("new pthread_t error!");
        exit(1);
    }

    // 初始化信号量
    ret = sem_init(&sem_add_request, 0, 1);
    if(ret == -1) {
        fprintf(stderr, "sem_add_request sem_init() error: %s\n", strerror(ret));
        exit(1);
    }

    ret = sem_init(&sem_request_num, 0, 0);
    if(ret == -1) {
        fprintf(stderr, "sem_request_num sem_init() error: %s\n", strerror(ret));
        exit(1);
    }

    // 创建线程池中的线程
    for (int i = 0; i < threads_num; ++i) {
        ret = pthread_create(&threads[i], NULL, request_func, this);
        if (ret != 0) {
            fprintf(stderr, "pthread_create error: %s\n", strerror(ret));
            exit(1);
        }

        // 设置为分离线程，自动去回收资源
        ret = pthread_detach(threads[i]);
        if (ret != 0) {
            fprintf(stderr, "pthread_detach error: %s\n", strerror(ret));
            exit(1);
        }
    }
}

template <typename T>
ThreadPool<T>::~ThreadPool() {
    int ret = 0;

    is_end = true;
    delete [] threads;

    // 销毁信号量
    ret = sem_destroy(&sem_add_request);
    if(ret == -1) {
        fprintf(stderr, "sem_add_request sem_destroy() error: %s\n", strerror(ret));
        exit(1);
    }

    ret = sem_destroy(&sem_request_num);
    if(ret == -1) {
        fprintf(stderr, "sem_request_num sem_destroy() error: %s\n", strerror(ret));
        exit(1);
    }
}

template <typename T>
bool ThreadPool<T>::add_request(T *request) {
    // 请求无效时直接返回
    if (!request) {
        return false;
    }

    // 添加请求时需对请求队列进行保护
    sem_wait(&sem_add_request);

    // 当请求队列已满时，添加失败，并且返回
    int req_que_size = request_queue.size();
    if (req_que_size > requests_max_num) {
        sem_post(&sem_add_request);
        return false;
    }
    // 请求队列未满则可添加请求，同时将队列中的请求数加一
    else {
        request_queue.push_back(request);
        sem_post(&sem_request_num);
    }

    sem_post(&sem_add_request);
    return true;
}

template <typename T>
void *ThreadPool<T>::request_func(void *arg) {
    // 向每个线程传入的参数是线程池
    ThreadPool *pool = (ThreadPool *)arg;
    // 如果is_end不为真，线程池中的线程进行请求处理，否则结束线程池
    while (!pool->is_end) {
        // 请求队列中的请求数减一
        sem_wait(&pool->sem_request_num);
        // 保护队列
        sem_wait(&pool->sem_add_request);
        
        // 取队首的请求进行处理
        T *request = pool->request_queue.front();
        pool->request_queue.pop_front();
        // 线程池中的线程执行的处理函数，需要在request的具体类中实现方法
        request->process();
        
        sem_post(&pool->sem_add_request);
    }
    return NULL;
}

#endif
```

线程池类定义为模板类来实现，它的一个实例可以是处理http请求并进行相应的`http_process`类，模板类的任务入口函数为`void process()`，由线程池中的线程执行。

#### 3. 实现I/O处理单元

服务器编程基本框架的I/O处理单元在`main.cpp`中实现，使用epoll和LT触发模式进行I/O处理，代码如下：

```C++
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
    // 将服务器的fd添加到epoll对象中
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
```

### 效果截图

**游戏界面：**

![6.png](./Image/6.png)

**被巡逻兵抓到，巡逻兵攻击，倒地游戏失败：**

![7.png](./Image/7.png)

![8.png](./Image/8.png)

**收集完全部宝箱，游戏成功：**

![9.png](./Image/9.png)

**巡逻兵偏离巡逻轨迹，开始追踪：**

![10.png](./Image/10.png)
