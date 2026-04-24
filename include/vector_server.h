#ifndef VECTOR_SERVER
#define VECTOR_SERVER
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
//
#include <string>
#include <cstring>
#include <iostream>
#include "vector_store.h"
#include "file_manager.h"
#include "similarities.hpp"
#include "command_parser.h"
#include "types.h"
//
#define BACKLOG (10) // max no of client waiting in queue
class Vector_Server
{
public:
    //
    Vector_Server(std::string port, Vector_store &, File_manager &);
    ~Vector_Server();
    // handels core-functionality
    bool setup();
    void run();
    void stop();
    //

private:
    int server_fd;
    std::string port_num;
    void handle_client(int client_fd);
    // Vector_server is dependent upon both, vector_store and file_manager
    Vector_store &vector_store;
    File_manager &file_manager;
};
//  -----------Both below structs are for refrence only, and are predefined.-------------
// struct sockaddr
// {
//     unsigned short sa_family; // address family, AF_xxx
//     char sa_data[14];         // 14 bytes of protocol address
// };
// struct addrinfo
// {
//     int ai_flags;             // AI_PASSIVE
//     int ai_family;            // AF_UNSPEC
//     int ai_socktype;          // SOCK_STREAM
//     int ai_protocol;          // 0
//     size_t ai_addrlen;        // size of ai_addr in bytes
//     struct sockaddr *ai_addr; // struct sockaddr_in or _in6
//     char *ai_canonname;
//     struct addrinfo *ai_next; // linked list, next node
// };

#endif
/*
        The Reverse-Proxy Problem:
            Document this thoroughly and also provide commands to enable the solution in readme.md
        The Format:
            In final documentation and readme, state this very cleanly

*/