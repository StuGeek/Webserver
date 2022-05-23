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
#include "Webserver.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        Webserver server;
        server.start_server();
    }
    else {
        // 记录端口号
        int port = atoi(argv[1]);
        Webserver server(port);
        server.start_server();
    }
}