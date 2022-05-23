#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <pthread.h>

#include "log.h"

using namespace std;

Log::Log() {
    fp_log = NULL;
    line_count = 0;
    is_async = false;
    is_end = false;
}

Log::~Log() {
    int ret = 0;

    is_end = true;
    if (fp_log != NULL) {
        fclose(fp_log);
    }

    // 销毁信号量
    ret = sem_destroy(&sem_que_size);
    if(ret == -1) {
        // fprintf(stderr, "sem_req_num sem_destroy() error: %s\n", strerror(ret));
        LOG_ERROR("%s%s", "sem_req_num sem_destroy() error: ", strerror(ret))
        exit(1);
    }
}

// 如果设置了异步写入日志，初始化异步过程
void Log::init_async_process(int log_que_capacity, int threads_num) {
    int ret = 0;
    
    is_async = true;

    que_capacity = log_que_capacity;
    if (que_capacity < 1) {
        // printf("The que_capacity of queue cann't less than 1\n");
        LOG_ERROR("%s", "The que_capacity of queue cann't less than 1")
        exit(-1);
    }

    ret = sem_init(&sem_que_size, 0, 0);
    if(ret == -1) {
        // fprintf(stderr, "sem_que_size sem_init() error: %s\n", strerror(ret));
        LOG_ERROR("%s%s", "sem_que_size sem_init() error: ", strerror(ret));
        exit(1);
    }

    // 创建线程数组
    try {
        threads = new pthread_t[threads_num];
    } catch(const std::bad_alloc &e) {
        // printf("new log pthread_t error!");
        LOG_ERROR("%s", "new log pthread_t error!");
        exit(1);
    }

    // 创建处理日志线程数组中的线程
    for (int i = 0; i < threads_num; ++i) {
        ret = pthread_create(&threads[i], NULL, async_write_log_func, NULL);
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

// 异步写入日志的线程执行函数
void *Log::async_write_log_func(void *args) {
    Log::get_instance()->async_write();
    return NULL;
}

void Log::async_write() {
    char *log_content = NULL;
    // 从日志队列中取出一个日志内容，写入文件    
    while (!is_end) {
        sem_wait(&sem_que_size);
        pthread_mutex_lock(&mutex_que);

        log_content = log_queue.front();
        log_queue.pop();

        pthread_mutex_lock(&logfile_mutex);
        fputs(log_content, fp_log);
        pthread_mutex_unlock(&logfile_mutex);
        
        pthread_mutex_unlock(&mutex_que);
    }
}

Log *Log::get_instance() {
    static Log logger;
    return &logger;
}

// 根据日志路径名、日志缓冲区大小、最大行数、日志队列最大长度以及异步线程数初始化日志类
bool Log::init(const char *file_path, bool is_log_open, 
                int log_buffer_size, int lines_max_num,
                    int log_que_capacity, int threads_num) {

    if (!is_log_open) {
        return false;
    }

    // 如果设置了日志队列的容量，那么设置异步写日志
    if (log_que_capacity > 0) {
        init_async_process(log_que_capacity, threads_num);
    }
    
    this->is_log_open = is_log_open;
    this->log_buffer_size = log_buffer_size;
    this->lines_max_num = lines_max_num;
    log_buffer = new char[log_buffer_size];
    memset(log_buffer, '\0', log_buffer_size);

    // 获取当前时间并转换成本地时间
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);

    // 获取路径名的最后一个/和其之后的内容，即文件名
    const char *flie_name = strrchr(file_path, '/');
    char file_full_name[1024] = {0};

    // 如果找不到最后一个/，那么表示文件放到当前路径，加上当前时间形成完整文件名
    if (flie_name == NULL) {
        snprintf(file_full_name, 1023, "%d_%02d_%02d_%s", tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday, file_path);
    }
    // 否则将最后一个/之后的文件名复制到log_name中，包含/的路径名复制到dir_name，加上时间形成完整文件名
    else {
        strcpy(log_name, flie_name + 1);
        strncpy(dir_name, file_path, flie_name - file_path + 1);
        snprintf(file_full_name, 1023, "%s%d_%02d_%02d_%s", dir_name, tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday, log_name);
    }

    // 获取当天是哪一天
    today = tm_now->tm_mday;
    // 以向末尾添加数据的模式打开要写入的日志文件
    fp_log = fopen(file_full_name, "a");
    if (fp_log == NULL) {
        return false;
    }

    return true;
}

// 根据日志级别和格式，向文件中写入日志内容
void Log::write_log(int log_level, const char *format, ...) {
    // 获取当前时间
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);

    time_t now_sec = now.tv_sec;
    struct tm *tm_now_sec = localtime(&now_sec);
    char level_content[10] = {0};
    switch (log_level) {
        case 0:
            strcpy(level_content, "[debug]:");
            break;
        case 1:
            strcpy(level_content, "[info]:");
            break;
        case 2:
            strcpy(level_content, "[warn]:");
            break;
        case 3:
            strcpy(level_content, "[error]:");
            break;
        default:
            strcpy(level_content, "[info]:");
            break;
    }

    // 对日志文件加锁后，写入一个日志内容
    pthread_mutex_lock(&logfile_mutex);

    // 日志行数加一
    line_count++;
    // 如果现在时间所在天和之前初始化时记录的所在天不同，或者当天有份日志已经写满，那么要另外生成一份当天的日志
    if (today != tm_now_sec->tm_mday || line_count % lines_max_num == 0) {
        char new_file_full_name[1024] = {0};
        fflush(fp_log);
        // 关掉原来日志
        fclose(fp_log);
        char tail_content[16] = {0};
       
        snprintf(tail_content, 16, "%d_%02d_%02d_", tm_now_sec->tm_year + 1900, tm_now_sec->tm_mon + 1, tm_now_sec->tm_mday);

        // 如果是现在时间所在天和之前初始化时记录的所在天不同
        if (today != tm_now_sec->tm_mday) {
            snprintf(new_file_full_name, 1023, "%s%s%s", dir_name, tail_content, log_name);
            // 更新现在所在天和日志行数
            today = tm_now_sec->tm_mday;
            line_count = 0;
        }
        // 如果只是有份日志已经写满，不用更新现在所在天和日志行数
        else {
            snprintf(new_file_full_name, 1023, "%s%s%s.%lld", dir_name, tail_content, log_name, line_count / lines_max_num);
        }
        // 打开新的文件
        fp_log = fopen(new_file_full_name, "a");
    }
 
    pthread_mutex_unlock(&logfile_mutex);

    va_list ap;
    va_start(ap, format);

    char *log_content;
    pthread_mutex_lock(&logfile_mutex);

    // 写入时间和具体内容到写缓冲区中
    int time_len = snprintf(log_buffer, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     tm_now_sec->tm_year + 1900, tm_now_sec->tm_mon + 1, tm_now_sec->tm_mday,
                     tm_now_sec->tm_hour, tm_now_sec->tm_min, tm_now_sec->tm_sec, now.tv_usec, level_content);
    
    int content_len = vsnprintf(log_buffer + time_len, log_buffer_size - 1, format, ap);
    log_buffer[time_len + content_len] = '\n';
    log_buffer[time_len + content_len + 1] = '\0';
    log_content = log_buffer;

    pthread_mutex_unlock(&logfile_mutex);

    // 如果选择了异步写入
    if (is_async) {
        pthread_mutex_lock(&mutex_que);

        // 当日志队列已满时，添加日志内容失败，并且返回
        int size = log_queue.size();
        if (size >= que_capacity) {
            pthread_mutex_unlock(&mutex_que);
            va_end(ap);
            return;
        }

        // 日志队列未满则可添加日志，同时将队列中的日志数加一，由线程来处理写入文件
        log_queue.push(log_content);
        sem_post(&sem_que_size);

        pthread_mutex_unlock(&mutex_que);
    }
    // 否则直接将日志内容写入文件里面
    else {
        pthread_mutex_lock(&logfile_mutex);
        fputs(log_content, fp_log);
        pthread_mutex_unlock(&logfile_mutex);
    }

    va_end(ap);
}

// 对日志文件加锁后，强制刷新写缓冲区，将内容写入日志文件
void Log::fflush_to_log() {
    pthread_mutex_lock(&logfile_mutex);
    fflush(fp_log);
    pthread_mutex_unlock(&logfile_mutex);
}

bool Log::get_is_log_open() { return is_log_open; }
