#ifndef VECTOR_SERVER
#define VECTOR_SERVER
// necessary libraries for server creation.
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
// necessary libraries for rest of logic.
#include <string>
#include <cstring>
#include <iostream>
#include "vector_store.h"   // for RAM updates/access
#include "file_manager.h"   // for file/database updates/access
#include "similarities.hpp" // for similarity searches
#include "command_parser.h" // for parsing logic
#include "types.h"          // for all structs used though out other files
#include "ivf.h"            // for IVF
//
#define BACKLOG (10) // max no of client waiting in queue
class Vector_Server
{
public:
    /**
     * @brief Initializes the TCP listener utilizing provided storage and file management engines.
     * @param port Target network port string for binding incoming client connections
     * @param V_store Active in-memory storage engine instance
     * @param F_manager Initialised binary file handler instance
     * @param conditions Immutable environment configuration struct dictating operational parameters
     */
    Vector_Server(std::string port, Vector_store &, File_manager &, const Config);
    ~Vector_Server();
    // --- Core-Functionality
    /**
     * @brief Configures the network socket and binds to the designated target port.
     * @return True upon successful socket binding, false if address resolution fails
     * @warning Silently sets internal socket descriptor to negative one on failure
     */
    bool setup();
    /**
     * @brief Initiates the blocking listener loop to accept client connections sequentially.
     * @note Executes an initial auto-load of persistent database records before listening
     * @warning Terminates the entire program via exit if socket listening fails
     */
    void run();
    /**
     * @brief Safely terminates the active network server socket connection.
     * @note Safe to invoke multiple times or if the socket remains uninitialized
     */
    void stop();

private:
    int server_fd;
    const Config con;
    std::string port_num;
    /**
     * @brief Processes and executes line-delimited text commands from an active socket.
     * @param client_fd Open file descriptor bound to the communicating client
     * @note Requires incoming command strings to terminate with a newline character
     * @warning Disconnects and closes the client socket immediately upon loop termination
     */
    void handle_client(int client_fd);
    // Vector_server is dependent upon both, vector_store and file_manager
    Vector_store &vector_store;
    File_manager &file_manager;
    IVF_index ivf_index_{100, 5};
};
//  -----------Both below structs are for refrence only, and are predefined.-------------
// use a guide to fully understand this prebuilt code.
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