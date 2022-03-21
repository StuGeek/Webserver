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
