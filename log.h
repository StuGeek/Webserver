#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include <semaphore.h>
#include <queue>

using namespace std;

class Log {
public:
    static Log *get_instance();
    // 根据日志路径名、日志缓冲区大小、最大行数、日志队列最大长度以及异步线程数初始化日志类
    bool init(const char *file_path, bool is_log_open, 
                int log_buffer_size = 10000, int lines_max_num = 10000000, 
                    int log_que_capacity = 0, int threads_num = 8);
    // 根据日志级别和格式，向文件中写入日志内容
    void write_log(int log_level, const char *format, ...);
    void fflush_to_log();    // 对日志文件加锁后，强制刷新写缓冲区，将内容写入日志文件
    bool get_is_log_open();

private:
    Log();
    ~Log();
    void init_async_process(int log_que_capacity, int threads_num = 8);  // 如果设置了异步写入日志，初始化异步过程
    static void *async_write_log_func(void *args);  // 异步写入日志的线程执行函数
    void async_write();  // 从日志队列中取出一个日志，异步写入文件 

private:
    char dir_name[500];             // 日志文件路径名
    char log_name[500];             // 日志文件名
    int log_buffer_size;            // 存储日志的缓冲区大小
    char *log_buffer;               // 存储日志内容的缓冲区
    long long int lines_max_num;    // 日志最大行数
    long long int line_count;       // 日志当前行数
    int today;                      // 记录当前时间是那一天
    FILE *fp_log;                   // 日志文件的文件指针
    bool is_async;                  // 是否异步写入日志
    pthread_mutex_t logfile_mutex;  // 对日志文件进行操作时使用的锁
    bool is_log_open;               // 是否打开日志功能
    bool is_end;                    // 异步写入日志的线程是否结束

    queue<char *> log_queue;        // 存储日志的队列，可以将其中的日志异步写入文件
    int que_capacity;               // 日志队列容量
    pthread_t *threads;             // 处理日志队列的线程

    pthread_mutex_t mutex_que;      // 操作日志队列时用到的互斥锁
    sem_t sem_que_size;             // 表示日志队列中元素个数的信号量
};

#define LOG_DEBUG(format, ...) if(Log::get_instance()->get_is_log_open()) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->fflush_to_log();}
#define LOG_INFO(format, ...) if(Log::get_instance()->get_is_log_open()) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->fflush_to_log();}
#define LOG_WARN(format, ...) if(Log::get_instance()->get_is_log_open()) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->fflush_to_log();}
#define LOG_ERROR(format, ...) if(Log::get_instance()->get_is_log_open()) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->fflush_to_log();}

#endif
