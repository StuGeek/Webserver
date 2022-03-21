#ifndef _HTTP_PROCESS_H_
#define _HTTP_PROCESS_H_

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

class http_process {
public:
    // 默认使用的http版本
    const char* default_protocol_version = "HTTP/1.1";
    // 服务器获取文件的根目录
    const char* root_path = "./resources";

    // HTTP请求方法
    enum HTTP_REQUEST_METHOD {
        GET,      // 请求指定的页面信息，并返回实体主体
        HEAD,     // 类似于GET请求，只不过返回的响应中没有具体的内容，用于获取报头
        POST,     // 向指定资源提交数据进行处理请求
        PUT,      // 从客户端向服务器传送的数据取代指定的文档的内容
        DELETE,   // 请求服务器删除指定的页面
        TRACE,    // 回显服务器收到的请求，主要用于测试或诊断
        OPTIONS,  // 允许客户端查看服务器的性能
        CONNECT,  // HTTP/1.1 协议中预留给能够将连接改为管道方式的代理服务器
        PATCH     // 是对PUT方法的补充，用来对已知资源进行局部更新
    };
    
    // 请求的解析状态 
    enum REQUEST_PARSING_STATE {
        REQUEST_PARSING_STATE_LINE,    // 正在解析请求行
        REQUEST_PARSING_STATE_HEADER,  // 正在解析请求头
        REQUEST_PARSING_STATE_BODY     // 正在解析请求体
    };
    
    // 请求解析的可能结果
    enum REQUEST_PARSING_RESULT {
        REQUEST_COMPLETE,              // 请求已经完整读取
        REQUEST_NOT_COMPLETE,          // 请求读取不完整，继续读取客户端数据
        REQUEST_FILE_SUCCESS,          // 请求文件成功，状态码返回200
        REQUEST_SYNTAX_ERROR,          // 请求存在语法错误，状态码返回400
        REQUEST_FORBIDDEN,             // 请求的客户端访问权限不够，不能访问资源，状态码返回403
        REQUEST_NO_FILE,               // 请求的文件不存在，状态码返回404
        SERVER_INTERNAL_ERROR,         // 服务器内部出错，状态码返回500
        CONNECTION_CLOSED              // 客户端关闭连接
    };
    
    // 读取每一行数据时的状态
    enum READ_LINE_STATUS {
        READ_LINE_COMPLETE,             // 读取到了完整的一行
        READ_LINE_NOT_COMPLETE,         // 读取的行数据还不完整，还需继续
        READ_LINE_BAD                   // 读取出错
    };

public:
    http_process() {}
    ~http_process() {}

    void init_process(int sockfd, const sockaddr_in& addr);  // 初始化对http的处理过程
    void end_process();  // 结束处理
    void process();      // 由线程池中的线程调用，处理http请求
    bool read();         // 以非阻塞方式读数据
    bool write();        // 以非阻塞方式写数据

private:
    void init_process_data();  // 初始化对http进行处理时需要的数据

    // 读取处理分析http请求
    REQUEST_PARSING_RESULT process_read();
    /* 与读取分析http请求有关的函数 */
    READ_LINE_STATUS read_line();                             // 读取每一行数据
    REQUEST_PARSING_RESULT read_request_line(char *text);     // 读取解析请求行
    REQUEST_PARSING_RESULT read_request_headers(char *text);  // 读取解析请求头
    REQUEST_PARSING_RESULT read_request_body(char *text);     // 读取解析请求体
    REQUEST_PARSING_RESULT get_request_file();                // 获取请求文件

    // 填充处理http响应
    bool process_write(REQUEST_PARSING_RESULT read_result);
    /* 与填充处理http响应有关的函数 */
    bool write_response(const char* format, ...);    // 填充响应
    // 填充响应状态行
    bool write_response_status_line(const char *protocol_version, int status, const char* title);
    // 填充响应头
    bool write_response_headers(int content_length);
    // 填充响应体
    bool write_response_body(const char* content);

public:
    static const int FILENAME_MAX_LEN = 8190;   // 请求的文件名的最大长度
    static const int READ_BUFFER_SIZE = 1024;   // 读缓冲区的长度
    static const int WRITE_BUFFER_SIZE = 1024;  // 写缓冲区的长度

    static int epoll_fd;    // 所有socket上的事件由同一个epoll进行监听
    static int client_cnt;  // 所有客户端总共连接的数量

private:
    int socket_fd;            // 这个http连接到的socket
    sockaddr_in socket_addr;  // 连接对方的socket地址
    
    char read_buffer[READ_BUFFER_SIZE];  // 读缓冲区
    int read_index;                      // 读缓冲区中已经读入的数据的结尾的再往下一个位置
    int check_index;                     // 当前正在解析的字符在读缓冲区中的位置
    int line_start_index;                // 当前正在解析的行的内容在读缓存区中的起始位置

    REQUEST_PARSING_STATE request_parsing_state;  // 解析请求不同部分的状态机所处的状态
    HTTP_REQUEST_METHOD request_method;           // http请求方法
    char *request_url;                            // 请求地址
    char *request_version;                        // http协议版本，这里只支持HTTP/1.1

    char request_file_path[FILENAME_MAX_LEN];  // 客户端请求的文件的路径
    char *request_host;     // 主机名
    bool is_keep_alive;     // 请求是否需要保持连接
    int cache_max_age;      // 缓存最长时间
    char *user_agent;       // 用户代理
    char *accept;           // 浏览器可以接收的内容类型
    char *referer;          // 当前请求URL是在什么地址中引用的
    char *accept_encoding;  // 浏览器可以处理的编码方式
    char *accept_language;  // 浏览器接收的语言
    int content_length;     // 请求消息长度

    char write_buffer[WRITE_BUFFER_SIZE];  // 写缓冲区
    int write_index;                       // 写缓冲区中待发送的字节数
    char* request_file_address;            // 请求的文件被映射到内存中的起始位置
    struct stat request_file_stat;         // 请求的文件状态
    
    struct iovec write_iov[2];             // 向量I/O缓冲区，第一个缓冲区存放响应信息，第二个缓冲区存放可能会获取的请求文件的内存起始地址
    int write_iovcnt;                      // 要写的向量I/O缓冲区数目
    size_t response_bytes_to_send;         // 将要发送的响应内容的字节数
    size_t response_bytes_have_send;       // 已发送的响应内容的字节数
};

#endif
