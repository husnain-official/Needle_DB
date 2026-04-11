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
//  -----------Both below structs are for refrence only, and is predefined.-------------
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