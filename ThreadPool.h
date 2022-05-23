#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <list>
#include "Mysql_connpool.h"
#include "log.h"

// 线程池类，将它定义为模板类是为了代码复用，模板参数T是任务类
template<typename T>
class ThreadPool {
public:
    // 线程池中的线程数默认为8，请求队列中的最大请求数默认为50000
    ThreadPool(Mysql_Connpool *connpool, int threads_num = 8, int req_max_num = 50000);
    ~ThreadPool();
    bool add_req(T *req);            // 向请求队列添加请求

private:
    static void *run(void *arg);     // 线程运行函数
    void req_func();                 // 每个任务请求执行的函数

private:
    int threads_num;                 // 线程数量
    int req_max_num;                 // 请求队列中的最大请求数
    pthread_t *threads;              // 线程池的数组
    std::queue<T *> req_que;         // 请求队列
    Mysql_Connpool *mysql_connpool;  // 数据库连接池

    bool is_end;                     // 是否结束线程池
    pthread_mutex_t mutex_req_que;   // 添加请求时用到的信号量
    sem_t sem_req_num;               // 表示请求队列中请求数的信号量               
};

template <typename T>
ThreadPool<T>::ThreadPool(Mysql_Connpool *connpool, int threads_num, int req_max_num) :
        mysql_connpool(connpool), threads_num(threads_num), req_max_num(req_max_num),
        threads(NULL), is_end(false) {

    int ret = 0;

    // 线程池中的线程数要大于0
    if (threads_num <= 0) {
        // printf("The number of threads should be more than zero!\n");
        LOG_ERROR("The number of threads should be more than zero!");
        exit(1);
    }

    // 请求队列中的最大请求数要大于0
    if (req_max_num <= 0) {
        // printf("The max number of requests should be more than zero!\n");
        LOG_ERROR("The max number of requests should be more than zero!");
        exit(1);
    }

    // 创建线程数组
    try {
        threads = new pthread_t[threads_num];
    } catch(const std::bad_alloc &e) {
        // printf("new pthread_t error!\n");
        LOG_ERROR("new pthread_t error!");
        exit(1);
    }

    ret = sem_init(&sem_req_num, 0, 0);
    if(ret == -1) {
        // fprintf(stderr, "sem_req_num sem_init() error: %s\n", strerror(ret));
        LOG_ERROR("%s%s", "sem_req_num sem_init() error: ", strerror(ret));
        exit(1);
    }

    // 创建线程池中的线程
    for (int i = 0; i < threads_num; ++i) {
        ret = pthread_create(&threads[i], NULL, run, this);
        if (ret != 0) {
            // fprintf(stderr, "pthread_create error: %s\n", strerror(ret));
            LOG_ERROR("%s%s", "pthread_create error: ", strerror(ret));
            exit(1);
        }

        // 设置为分离线程，自动去回收资源
        ret = pthread_detach(threads[i]);
        if (ret != 0) {
            // fprintf(stderr, "pthread_detach error: %s\n", strerror(ret));
            LOG_ERROR("%s%s", "pthread_detach error: ", strerror(ret));
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
    ret = sem_destroy(&sem_req_num);
    if(ret == -1) {
        // fprintf(stderr, "sem_req_num sem_destroy() error: %s\n", strerror(ret));
        LOG_ERROR("%s%s", "sem_req_num sem_destroy() error: ", strerror(ret));
        exit(1);
    }
}

template <typename T>
bool ThreadPool<T>::add_req(T *req) {
    // 请求无效时直接返回
    if (!req) {
        return false;
    }

    // 添加请求时需对请求队列进行保护
    pthread_mutex_lock(&mutex_req_que);

    // 当请求队列已满时，添加失败，并且返回
    int req_que_size = req_que.size();
    if (req_que_size > req_max_num) {
        pthread_mutex_unlock(&mutex_req_que);
        return false;
    }
    // 请求队列未满则可添加请求，同时将队列中的请求数加一
    else {
        req_que.push(req);
        sem_post(&sem_req_num);
    }

    pthread_mutex_unlock(&mutex_req_que);
    return true;
}

template <typename T>
void *ThreadPool<T>::run(void *arg) {
    ThreadPool *pool = (ThreadPool *)arg;
    pool->req_func();
    return NULL;
}

template <typename T>
void ThreadPool<T>::req_func() {
    T *req = NULL;
    // 如果is_end不为真，线程池中的线程进行请求处理，否则结束线程池
    while (!is_end) {
        // 请求队列中的请求数减一
        sem_wait(&sem_req_num);
        // 保护队列
        pthread_mutex_lock(&mutex_req_que);
        
        // 取队首的请求进行处理
        req = req_que.front();
        req_que.pop();
        Mysql_conn_RAII mysql_coon(&req->mysql, mysql_connpool);
        // 线程池中的线程执行的处理函数，需要在req的具体类中实现方法
        req->process();
        
        pthread_mutex_unlock(&mutex_req_que);
    }
}

#endif
