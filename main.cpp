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
#include "Webserver.h"

const int DEFAULT_PORT = 8000;  // 默认端口号

int main(int argc, char *argv[]) {
    // 记录端口号
    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    printf("server port: %d\n", port);

    Webserver webserver(port);
    webserver.start_server();
}