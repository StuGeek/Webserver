#include "http_process.h"
#include <iostream>
#include <string.h>

using namespace std;

// http状态码
enum HTTP_STATUS_CODE {
    SUCCESS_200,       // 2开头的状态码代表操作被成功接收并处理，200代表请求成功
    CLIENT_ERROR_400,  // 4开头的状态码代表客户端错误，400代表请求的语法错误，服务器无法理解
    CLIENT_ERROR_403,  // 403代表请求被服务器拒绝了
    CLIENT_ERROR_404,  // 404代表未找到资源
    SERVER_ERROR_500   // 5开头的状态码代表服务器错误，500代表服务器内部错误，无法完成请求
};

// 状态结构体
struct {
    HTTP_STATUS_CODE code;    // 状态码
    const char *message;      // 状态消息
    const char *description;  // 状态含义描述
} status[5] = {
    {
        SUCCESS_200,
        "OK",
        "200 The server successfully processed the request!"
    },
    {
        CLIENT_ERROR_400,
        "Bad Request",
        "400 The request's syntax is incorrect and the server cannot understand it!"
    },
    {
        CLIENT_ERROR_403,
        "Forbidden",
        "403 The request was rejected by the server!"
    },
    {
        CLIENT_ERROR_404,
        "Not Found",
        "404 Resource not found!"
    },
    {
        SERVER_ERROR_500,
        "Internal Server Error!",
        "500 Server internal error, unable to complete the request!"
    }
};

// 所有的客户端数
int http_process::client_cnt = 0;
// 所有socket上的事件由同一个epoll进行监听
int http_process::epoll_fd = -1;

// 向epoll里添加监听的文件描述符，并设置为非阻塞，注册读就绪事件
extern void epoll_add_fd(int epoll_fd, int listen_fd, bool is_oneshot) {
    // 将文件描述符设置为非阻塞
    int flags = fcntl(listen_fd, F_GETFL);
    if (fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        printf("fcntl set nonblock error!\n");
        exit(1);
    }

    epoll_event event;
    event.events = EPOLLIN | EPOLLRDHUP;

    if (is_oneshot) {
        event.events |= EPOLLONESHOT;
    }

    event.data.fd = listen_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event);
}

// 从epoll中删除监听的文件描述符
void epoll_delete_fd(int epoll_fd, int listen_fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, listen_fd, NULL);
    close(listen_fd);
}

// 修改epoll里的文件描述符
void epoll_modify_fd(int epoll_fd, int listen_fd, int events) {
    epoll_event event;
    // 重置EPOLLONESHOT以确保下一次可读时能够被触发
    event.events = events | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    
    event.data.fd = listen_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, listen_fd, &event);
}

// 初始化处理http的基本数据
void http_process::init_process_data() {
    read_index = 0;
    check_index = 0;
    line_start_index = 0;

    // 默认状态为解析请求行
    request_parsing_state = REQUEST_PARSING_STATE_LINE;
    // 默认的http请求方式为GET
    request_method = GET;
    request_url = NULL;              
    request_version = NULL;

    request_host = NULL;
    // 默认不保持连接
    is_keep_alive = false;
    cache_max_age = 0;
    user_agent = NULL;
    accept = NULL;
    referer = NULL;
    accept_encoding = NULL;
    accept_language = NULL;
    content_length = 0;

    is_method_post = false;
    write_index = 0;

    response_bytes_to_send = 0;
    response_bytes_have_send = 0;

    memset(&read_buffer, 0, sizeof(read_buffer));
    memset(&write_buffer, 0, sizeof(write_buffer));
    memset(&request_file_path, 0, sizeof(request_file_path));
}

// 初始化连接,外部调用初始化套接字地址
void http_process::init_process(int sockfd, const sockaddr_in& addr){
    this->socket_fd = sockfd;
    this->socket_addr = addr;
    mysql = NULL;
    
    int ret = 0;

    // 对处于TIME_WAIT状态下的socket，设置端口复用
    int opt = 1; 
    ret = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); 
    if (ret < 0) {
        printf("http_process init_process setsockopt error!\n");
        exit(1);
    }
    epoll_add_fd(epoll_fd, sockfd, true);
    // 客户端数量加一
    client_cnt++;
    // 初始化处理http的基本数据
    init_process_data();
}

// 结束处理
void http_process::end_process() {
    if (socket_fd == -1) {
        return;
    }
    // 从epoll中删除监听的文件描述符
    epoll_delete_fd(epoll_fd, socket_fd);
    socket_fd = -1;
    // 客户端连接数量减一
    client_cnt--;
}

// 循环读取客户端数据
bool http_process::read() {
    // 如果读入的数据超过了读缓冲区的大小，那么返回失败
    if (read_index >= READ_BUFFER_SIZE) {
        return false;
    }

    // 读取字节大小
    int read_bytes = 0;
    if (trig_mode == 0) {
        // 从read_buffer的read_index处开始读入数据到缓冲区中，返回读取字节大小
        read_bytes = recv(socket_fd, &read_buffer[read_index], READ_BUFFER_SIZE - read_bytes, 0);
        if (read_bytes == -1) {
            return false;
        }

        // 加上这一次读取的数据大小以更新下一次读取的起始位置
        read_index += read_bytes;
        return true;
    }
    else {
        while (true) {
            // 从read_buffer的read_index处开始读入数据到缓冲区中，返回读取字节大小
            read_bytes = recv(socket_fd, &read_buffer[read_index], READ_BUFFER_SIZE - read_bytes, 0);
            if (read_bytes == -1) {
                // 非阻塞读时没有数据，说明读完，退出循环
                if(errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                // 其它错误则直接返回错误
                return false;
            }
            // 对方关闭连接，直接返回错误
            else if (read_bytes == 0) {
                return false;
            }
            // 加上这一次读取的数据大小以更新下一次读取的起始位置
            read_index += read_bytes;
        }

        return true;
    }
}

// 读入解析http请求
http_process::REQUEST_PARSING_RESULT http_process::process_read() {
    REQUEST_PARSING_RESULT ret;
    READ_LINE_STATUS line_status = READ_LINE_COMPLETE;

    char* line_content = NULL;
    // 当在读入请求体状态下且之前读取到了一行完整的数据，或者在其它状态下读入到了一行完整的数据
    while ((request_parsing_state == REQUEST_PARSING_STATE_BODY && line_status == READ_LINE_COMPLETE)
                || (line_status = read_line()) == READ_LINE_COMPLETE) {
        // 获取一行数据的内容
        line_content = &read_buffer[line_start_index];
        // 设置读取下一行数据的起始位置为检查的位置
        line_start_index = check_index;
        // printf("%s\n", line_content);

        // 判断主状态机状态
        switch (request_parsing_state) {
            // 当主状态机在读入请求行时
            case REQUEST_PARSING_STATE_LINE: {
                // 读入获取到的那行数据
                ret = read_request_line(line_content);
                // 如果请求语法错误，那么直接结束返回
                if (ret == REQUEST_SYNTAX_ERROR) {
                    return REQUEST_SYNTAX_ERROR;
                }
                // 否则继续循环
                break;
            }
            // 当主状态机在读入请求头时
            case REQUEST_PARSING_STATE_HEADER: {
                ret = read_request_headers(line_content);
                // 如果请求语法错误，那么直接返回
                if (ret == REQUEST_SYNTAX_ERROR) {
                    return REQUEST_SYNTAX_ERROR;
                // 如果获取到了完整的请求，那么读入具体信息并返回
                } else if (ret == REQUEST_COMPLETE) {
                    return get_request_file();
                }
                // 否则继续循环
                break;
            }
            // 当主状态机在读入请求体时
            case REQUEST_PARSING_STATE_BODY: {
                ret = read_request_body(line_content);
                // 如果获取到了完整的请求，那么读入具体信息并返回
                if (ret == REQUEST_COMPLETE) {
                    return get_request_file();
                }
                // 否则设置从状态机的状态为行数据不完整
                line_status = READ_LINE_NOT_COMPLETE;
                break;
            }
            // 否则返回内部错误
            default: {
                return SERVER_INTERNAL_ERROR;
            }
        }
    }
    // 主状态机返回请求不完整，继续读取数据
    return REQUEST_NOT_COMPLETE;
}

// 读入一行数据，按照格式，一行数据的结尾应该是\r\n
http_process::READ_LINE_STATUS http_process::read_line() {
    // 遍历已经在读缓存区中但还未分析的字符
    for ( ; check_index < read_index; ++check_index) {
        char check_char = read_buffer[check_index];
        // 如果当前分析的字符为'\r'
        if (check_char == '\r') {
            // 如果当前分析的字符是最后一个字符，但还未读到'\n'结束一行
            if (check_index + 1 == read_index) {
                // 那么返回行数据不完整
                return READ_LINE_NOT_COMPLETE;
            }
            // 如果当前分析的后一个字符是'\n'，说明读完一行，将\r\n所在位置置为\0
            else if (read_buffer[check_index + 1] == '\n') {
                read_buffer[check_index++] = '\0';
                read_buffer[check_index++] = '\0';
                // 返回读取到完整的一行
                return READ_LINE_COMPLETE;
            }
            // 否则说明'\r'后面没有跟字符或者跟的字符不是'\n'，返回读取出错
            else {
                return READ_LINE_BAD;
            }
        }
        // 如果当前分析的字符为'\n'
        else if (check_char == '\n')  {
            // 如果'\n'不是一行的前两个字符且前一个字符是'\r'，说明读完一行
            if (check_index > 1 && read_buffer[check_index - 1] == '\r') {
                // 将\r\n所在位置置为\0，返回读取到完整的一行
                read_buffer[check_index - 1] = '\0';
                read_buffer[check_index++] = '\0';
                return READ_LINE_COMPLETE;
            }
            // 否则说明'\n'前面没有字符或者字符不是'\r'，返回读取出错
            else {
                return READ_LINE_BAD;
            }
        }
    }
    // 返回行数据不完整
    return READ_LINE_NOT_COMPLETE;
}

// 读入http请求行，记录请求方法，请求资源的url,和http的版本号
http_process::REQUEST_PARSING_RESULT http_process::read_request_line(char* text) {
    // 获取请求方法的字符串
    char *request_method_str = strtok(text, " ");
    if (!request_method_str) {
        return REQUEST_SYNTAX_ERROR;
    }

    // 将请求方法的字符串转换为枚举类型并存储，忽略大小写
    if (strcasecmp(request_method_str, "GET") == 0) {
        request_method = GET;
    }
    else if (strcasecmp(request_method_str, "POST") == 0) {
        request_method = POST;
        is_method_post = true;
    }
    // 如果没有相应的请求方法，返回请求语法错误
    else {
        return REQUEST_SYNTAX_ERROR;
    }

    // 获取请求的url
    request_url = strtok(NULL, " ");
    if (!request_url) {
        return REQUEST_SYNTAX_ERROR;
    }
    // 去掉http://，和主机名再保存
    if (strncasecmp(request_url, "http://", 7) == 0) {   
        request_url += 7;
        request_url = strchr(request_url, '/');
    }

    // 去掉https://，和主机名再保存
    if (strncasecmp(request_url, "https://", 8) == 0) {   
        request_url += 8;
        request_url = strchr(request_url, '/');
    }

    if (!request_url || request_url[0] != '/') {
        return REQUEST_SYNTAX_ERROR;
    }

    // 获取请求的版本，这里只支持http/1.1版本
    request_version = strtok(NULL, "\n");
    if (!request_version) {
        return REQUEST_SYNTAX_ERROR;
    }
    if (strcasecmp(request_version, "HTTP/1.1") != 0) {
        return REQUEST_SYNTAX_ERROR;
    }

    // 后面没有接请求文件，默认返回登录界面
    if (strlen(request_url) == 1) {
        strcat(request_url, "login.html");
    }

    // 状态机检查状态变为正在分析请求头
    request_parsing_state = REQUEST_PARSING_STATE_HEADER;
    // 继续读取客户端数据
    return REQUEST_NOT_COMPLETE;
}

// 读入http请求头每一行的头部信息
http_process::REQUEST_PARSING_RESULT http_process::read_request_headers(char* text) {
    // 如果头部字段是请求资源所在的主机和端口
    if (strncasecmp(text, "Host:", 5) == 0) {
        // 跳过字段和空格，存储主机和端口信息
        text += 5;
        text += strspn(text, " ");
        request_host = text;
    }
    // 如果头部字段是请求连接管理
    else if (strncasecmp(text, "Connection:", 11) == 0) {
        // 跳过字段和空格，设置是否保持连接状态
        text += 11;
        text += strspn(text, " ");
        if (strcasecmp(text, "keep-alive") == 0) {
            is_keep_alive = true;
        }
    }
    // 如果头部字段是缓存最长时间
    else if (strncasecmp(text, "Cache-Control: max-age=", 23) == 0) {
        // 跳过字段和空格，记录缓存最长时间
        text += 23;
        text += strspn(text, " ");
        cache_max_age = atoi(text);
    }
    // 如果头部字段是用户代理
    else if (strncasecmp(text, "User-Agent:", 11) == 0) {
        // 跳过字段和空格，记录用户代理
        text += 11;
        text += strspn(text, " ");
        user_agent = text;
    }
    // 如果头部字段是浏览器可以接收的内容类型
    else if (strncasecmp(text, "Accept:", 7) == 0) {
        // 跳过字段和空格，记录浏览器可以接收的内容类型
        text += 7;
        text += strspn(text, " ");
        accept = text;
    }
    // 如果头部字段是当前请求URL是在什么地址中引用的
    else if (strncasecmp(text, "Referer:", 8) == 0) {
        // 跳过字段和空格，记录当前请求URL是在什么地址中引用的
        text += 8;
        text += strspn(text, " ");
        referer = text;
    }
    // 如果头部字段是浏览器可以处理的编码方式
    else if (strncasecmp(text, "Accept-Encoding:", 16) == 0) {
        // 跳过字段和空格，记录浏览器可以处理的编码方式
        text += 16;
        text += strspn(text, " ");
        accept_encoding = text;
    }
    // 如果头部字段是浏览器接收的语言
    else if (strncasecmp(text, "Accept-Language:", 16) == 0) {
        // 跳过字段和空格，记录浏览器接收的语言
        text += 16;
        text += strspn(text, " ");
        accept_language = text;
    }
    // 如果头部字段是请求消息长度
    else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        // 跳过字段和空格，记录请求消息长度
        text += 15;
        text += strspn(text, " ");
        content_length = atoi(text);
    }
    // 如果是空行那么请求头已完成读入
    else if(text[0] == '\0') {
        // 如果请求体的消息长度大于0，那么还需要读入请求体
        if (content_length != 0) {
            // 状态机检查状态变为正在分析请求体
            request_parsing_state = REQUEST_PARSING_STATE_BODY;
            // 继续读取客户端数据
            return REQUEST_NOT_COMPLETE;
        }
        // 否则请求读取已经完整
        return REQUEST_COMPLETE;
    }
    // 如果头部字段为其它那么打印无法识别
    else {
        // printf("This header can't be recognized: %s\n", text);
    }
    // 继续读取客户端数据
    return REQUEST_NOT_COMPLETE;
}

// 读入http请求的请求体，这里只判断请求体是否被完整读入
http_process::REQUEST_PARSING_RESULT http_process::read_request_body(char* text) {
    if (read_index >= check_index + content_length) {
        text[content_length] = '\0';
        request_body_data = text;
        // 请求体被完整读入则返回读取完整
        return REQUEST_COMPLETE;
    }
    // 否则返回请求不完整
    return REQUEST_NOT_COMPLETE;
}

// 获取请求文件，将文件映射进内存request_file_address位置
http_process::REQUEST_PARSING_RESULT http_process::get_request_file() {
    // 请求文件根路径
    strcpy(request_file_path, root_path);
    int root_path_len = strlen(root_path);
    
    // 如果请求方法是POST
    if (is_method_post) {
        char username[200];
        char password[200];

        // 提取用户名
        int username_begin_index = strlen("username=");
        int i = username_begin_index;
        while (request_body_data[i] != '&') {
            if (i - username_begin_index >= 200) {
                printf("The username is too long\n");
                return REQUEST_SYNTAX_ERROR;
            }
            username[i - username_begin_index] = request_body_data[i];
            i++;
        }
        username[i - username_begin_index] = '\0';

        // 提取密码
        i += strlen("&password=");
        int passwd_begin_index = i;
        while (request_body_data[i] != '\0') {
            if (i - passwd_begin_index >= 200) {
                printf("The password is too long\n");
                return REQUEST_SYNTAX_ERROR;
            }
            password[i - passwd_begin_index] = request_body_data[i];
            i++;
        }
        password[i - passwd_begin_index] = '\0';

        Mysql_Connpool *connpool = Mysql_Connpool::get_instance();

        // 如果是在登录界面发起的登录POST请求
        if (strncasecmp(request_url, "/login", 6) == 0) {
            // 如果用户名存在且密码正确，返回欢迎界面
            if (connpool->user_passwd.count(username) && connpool->user_passwd[username] == password) {
                strcat(request_file_path, "/welcome.html");
            }
            // 否则返回登录错误界面
            else {
                strcat(request_file_path, "/login_error.html");
            }
        }
        // 如果是在注册界面发起的注册POST请求
        else if (strncasecmp(request_url, "/register", 9) == 0) {
            // 如果用户名不存在
            if (!connpool->user_passwd.count(username)) {
                pthread_mutex_lock(&mutex_mysql_connpool);

                // 构建向数据库插入新的用户名和密码的sql语句
                char sql_statment[200];
                int save_len = strlen("INSERT INTO user(username, passwd) VALUES('', '')");
                
                if (save_len + strlen(username) + strlen(password) >= 200) {
                    printf("The username and password is too long!\n");
                    return REQUEST_SYNTAX_ERROR;
                }
                
                strcpy(sql_statment, "INSERT INTO user(username, passwd) VALUES('");
                strcat(sql_statment, username);
                strcat(sql_statment, "', '");
                strcat(sql_statment, password);
                strcat(sql_statment, "')");

                // 返回插入语句结果
                int res = mysql_query(mysql, sql_statment);
                connpool->user_passwd[username] = password;

                pthread_mutex_unlock(&mutex_mysql_connpool);
                
                // 如果插入失败，返回注册错误界面
                if (res) {
                    strcat(request_file_path, "/register_error.html");
                }
                // 否则返回登录界面
                else {
                    strcat(request_file_path, "/login.html");
                }
            }
            // 用户名存在则不能注册，返回注册错误界面
            else {
                strcat(request_file_path, "/register_error.html");
            }
        }
    }
    // 如果请求方法是GET
    else {
        // 将请求文件的url和根文件路径拼接，形成完整的的请求文件路径
        strncpy(request_file_path + root_path_len, request_url, FILENAME_MAX_LEN - root_path_len - 1);
    }
    
    // 如果请求文件不存在，返回相应的状态
    if (stat(request_file_path, &request_file_stat) < 0) {
        return REQUEST_NO_FILE;
    }

    // 如果没有对请求文件的访问权限，返回请求受限
    if (!(request_file_stat.st_mode & S_IROTH)) {
        return REQUEST_FORBIDDEN;
    }

    // 如果访问的路径不是文件而是目录，返回请求语法错误
    if (S_ISDIR(request_file_stat.st_mode)) {
        return REQUEST_SYNTAX_ERROR;
    }

    // 只读打开文件
    int fd = open(request_file_path, O_RDONLY);
    // 将文件以私有映射的形式映射进内存，内存区域的写入不会影响到原文件，页内容可以被读取
    request_file_address = (char*)mmap(0, request_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    // 返回请求文件成功
    return REQUEST_FILE_SUCCESS;
}

// 填充http响应
bool http_process::write() {
    int ret;
    // 如果将要发送的字节为0，那么响应结束
    if (response_bytes_to_send == 0) {
        // 设置监听读事件
        epoll_modify_fd(epoll_fd, socket_fd, EPOLLIN); 
        // 初始化处理http的基本数据
        init_process_data();
        // 返回写入成功
        return true;
    }

    // 循环写数据
    while(true) {
        // 将write_iov中write_iovcnt个缓冲区中的内容写入socket_fd代表的套接字中
        int write_bytes = writev(socket_fd, write_iov, write_iovcnt);
        // 如果写入失败
        if (write_bytes == -1) {
            // 如果错误代码不是EAGAIN，说明不是TCP写缓冲暂时没有空间导致的错误
            if(errno != EAGAIN) {
                // 如果请求文件成功，并在内存中有映射
                if(request_file_address) {
                    // 取消请求文件在内存中的映射并将请求文件的内存地址置为0
                    ret = munmap(request_file_address, request_file_stat.st_size);
                    if (ret < 0) {
                        printf("munmap error!\n");
                        exit(1);
                    }
                    request_file_address = 0;
                }
                // 返回写入数据失败
                return false;
            }
            // 否则继续等待下一次EPOLLOUT事件写入数据
            else {
                epoll_modify_fd(epoll_fd, socket_fd, EPOLLOUT);
                // 返回写入数据成功
                return true;
            }
        }

        // 更新已经写入和准备写入的消息大小
        response_bytes_have_send += write_bytes;
        response_bytes_to_send -= write_bytes;

        // 如果已经发送的字节大小大于等于第一个缓冲区中有关响应信息的内容大小
        if (response_bytes_have_send >= write_iov[0].iov_len) {
            // 说明第一个缓冲区中的内容已经发出，设置第一个缓冲区中内容的长度为0，表示发送完毕
            write_iov[0].iov_len = 0;
            // 更新第二个缓冲区的起始位置和内容大小
            write_iov[1].iov_base = request_file_address + response_bytes_have_send - write_index;
            write_iov[1].iov_len = response_bytes_to_send;
        }
        // 否则说明第一个缓冲区中的内容还没发送完
        else {
            // 更新第一个缓冲区中起始位置和内容长度
            write_iov[0].iov_base = write_buffer + response_bytes_have_send;
            write_iov[0].iov_len = write_iov[0].iov_len - write_bytes;
        }

        // 如果没有将要发送的数据，说明缓冲区中的内容已经全部发送完
        if (response_bytes_to_send <= 0) {
            // 如果请求文件成功，并在内存中有映射
            if(request_file_address) {
                // 取消请求文件在内存中的映射并将请求文件的内存地址置为0
                ret = munmap(request_file_address, request_file_stat.st_size);
                if (ret < 0) {
                    printf("munmap error!\n");
                    exit(1);
                }
                request_file_address = 0;
            }

            // 设置监听读事件
            epoll_modify_fd(epoll_fd, socket_fd, EPOLLIN);
            // 如果连接状态为保持连接
            if (is_keep_alive) {
                // 那么初始化处理http的基本数据
                init_process_data();
                // 返回写入数据成功
                return true;
            }
            // 否则写入数据失败
            else {
                return false;
            }
        }
    }
}

// 根据处理http请求的结果，填充返回给客户端响应的内容
bool http_process::process_write(REQUEST_PARSING_RESULT read_result) {
    switch (read_result) {
        // 如果读取http请求的处理结果为获取文件成功
        case REQUEST_FILE_SUCCESS:
            // 填充响应行的版本、状态码和状态码消息，状态码为200，表示请求成功
            write_response_status_line(default_protocol_version, 200, status[SUCCESS_200].message);
            // 填充响应头，消息长度为请求文件的大小
            write_response_headers(request_file_stat.st_size);
            // 将写缓冲区中的响应内容放进第一个向量I/O缓冲区中
            write_iov[0].iov_base = write_buffer;
            write_iov[0].iov_len = write_index;
            // 将请求文件的内存地址放进第二个向量I/O缓冲区中
            write_iov[1].iov_base = request_file_address;
            write_iov[1].iov_len = request_file_stat.st_size;
            // 设置需要写的缓冲区数目为2
            write_iovcnt = 2;

            // 准备发送的字节数为两个缓冲区中的内容总数
            response_bytes_to_send = write_index + request_file_stat.st_size;

            return true;
        // 如果请求http语法错误
        case REQUEST_SYNTAX_ERROR:
            // 填充响应行的版本、状态码和状态码消息，状态码为400，表示请求成功
            write_response_status_line(default_protocol_version, 400, status[CLIENT_ERROR_400].message);
            // 填充响应头，消息长度为400状态码消息的长度
            write_response_headers(strlen(status[CLIENT_ERROR_400].description));
            // 如果使用400状态码的消息填充响应体失败，直接返回
            if (!write_response_body(status[CLIENT_ERROR_400].description)) {
                return false;
            }
            break;
        // 如果没有访问权限
        case REQUEST_FORBIDDEN:
            // 填充响应行的版本、状态码和状态码消息，状态码为403，表示请求成功
            write_response_status_line(default_protocol_version, 403, status[CLIENT_ERROR_403].message);
            // 填充响应头，消息长度为403状态码消息的长度
            write_response_headers(strlen(status[CLIENT_ERROR_403].description));
            // 如果使用403状态码的消息填充响应体失败，直接返回
            if (!write_response_body(status[CLIENT_ERROR_403].description)) {
                return false;
            }
            break;
        // 如果服务器没有资源
        case REQUEST_NO_FILE:
            // 填充响应行的版本、状态码和状态码消息，状态码为404，表示请求成功
            write_response_status_line(default_protocol_version, 404, status[CLIENT_ERROR_404].message);
            // 填充响应头，消息长度为404状态码消息的长度
            write_response_headers(strlen(status[CLIENT_ERROR_404].description));
            // 如果使用404状态码的消息填充响应体失败，直接返回
            if (!write_response_body(status[CLIENT_ERROR_404].description)) {
                return false;
            }
            break;
        // 如果服务器内部错误
        case SERVER_INTERNAL_ERROR:
            // 填充响应行的版本、状态码和状态码消息，状态码为500，表示请求成功
            write_response_status_line(default_protocol_version, 500, status[SERVER_ERROR_500].message);
            // 填充响应头，消息长度为500状态码消息的长度
            write_response_headers(strlen(status[SERVER_ERROR_500].description));
            // 如果使用500状态码的消息填充响应体失败，直接返回
            if (!write_response_body(status[SERVER_ERROR_500].description)) {
                return false;
            }
            break;
        default:
            return false;
    }

    // 状态码400、403、404、500的消息填充成功后，将写缓冲区中的响应内容放进第一个向量I/O缓冲区中
    write_iov[0].iov_base = write_buffer;
    write_iov[0].iov_len = write_index;
    // 设置需要写的缓冲区数目为1
    write_iovcnt = 1;
    // 准备发送的字节数为第一个缓冲区中的内容总数
    response_bytes_to_send = write_index;
    return true;
}

// 向写缓冲区中写入响应信息等待发送
bool http_process::write_response(const char* format, ...) {
    // 如果写入的数据超过了写缓冲区的大小，那么返回失败
    if (write_index >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list ap;
    va_start(ap, format);
    // 返回按格式写入写缓冲区的内容长度
    int write_len = vsnprintf(write_buffer + write_index, WRITE_BUFFER_SIZE - write_index - 1, format, ap);
    // 如果写入长度大于写缓冲区的剩余大小，返回失败
    if(write_len >= WRITE_BUFFER_SIZE - write_index - 1) {
        va_end(ap);
        return false;
    }
    // 否则更新下一次写入的起始位置
    write_index += write_len;
    va_end(ap);
    return true;
}

// 填充响应状态行
bool http_process::write_response_status_line(const char *protocol_version, int status_code, const char* title) {
    return write_response("%s %d %s\r\n", protocol_version, status_code, title);
}

// 填充响应头
bool http_process::write_response_headers(int response_content_len) {
    // 向响应中填充内容长度、类型、连接状态和空行
    return write_response("Content-Length:%d\r\n", response_content_len) &&
           /*write_response("Content-Type:%s\r\n", "text/html") &&*/
           write_response("Connection:%s\r\n", is_keep_alive ? "keep-alive" : "close") &&
           write_response("%s", "\r\n");
}

// 填充响应体，这里直接将消息内容写入
bool http_process::write_response_body(const char *content) {
    return write_response("%s", content);
}

// 线程池中的线程调用的处理函数，对http请求进行处理
void http_process::process() {
    // 对http请求进行读取和处理
    REQUEST_PARSING_RESULT read_result = process_read();
    // 如果读取的结果为请求不完整，需要继续读取数据
    if (read_result == REQUEST_NOT_COMPLETE) {
        // 设置监听读事件，并直接返回
        epoll_modify_fd(epoll_fd, socket_fd, EPOLLIN);
        return;
    }
    
    // 否则对http请求生成相应的响应
    if (!process_write(read_result)) {
        // 如果处理生成响应失败，结束处理过程
        end_process();
    }
    // 设置监听写事件
    epoll_modify_fd(epoll_fd, socket_fd, EPOLLOUT);
}
