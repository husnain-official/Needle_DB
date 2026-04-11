#include <iostream>
#include "vector_store.h"
#include "file_manager.h"
#include "vector_server.h"
#include "../tests/integration/json.hpp"
#include <fstream>
using namespace std;
// Open Beejs guide, to understand this easily
// https://beej.us/guide/bgnet/html/split/system-calls-or-bust.html#getaddrinfoprepare-to-launch
int main()
{
    cout << "VectorDB   starting...\n";
    //
    // setup (necessary info for socket creation)
    addrinfo hints, *results, *node;
    int status = 0;
    memset(&hints, 0, sizeof(hints)); // Ensure 'hints' is empty
    hints.ai_family = AF_UNSPEC;      // Accept either 'IPv4' or 'IPv6'
    hints.ai_socktype = SOCK_STREAM;  // Sock-Stream will be used -> 2 way connection needed
    hints.ai_flags = AI_PASSIVE;      // Fill up my Ip address, (I am the server)
    // Safety-Check
    if ((status = getaddrinfo(NULL, "8080", &hints, &results)) != 0)
    {
        // 'cerr' if error, show it immediately. we use gai_strerror as it has its unique error codes
        cerr << gai_strerror(status);
        return 1;
    }
    // Find a valid socket and bind to it
    int server_fd;
    for (node = results; node != NULL; node = node->ai_next)
    {
        // server_fd -> socket file descriptor
        server_fd = socket(node->ai_family, node->ai_socktype, node->ai_protocol); // 0 standard protocol for stream -> TCP
        if (server_fd == -1)
        {
            perror("accept");
            continue;
        }
        // Allows immediate reuse of the port
        int yes = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        // bind
        if (bind(server_fd, node->ai_addr, node->ai_addrlen) == 0)
            break; // a working socket found and bounded :)
        close(server_fd);
    }
    if (node == NULL)
    {
        // we looped through everything and nothing worked, sad life :(
        cerr << "Failed to bind to any address" << endl;
        freeaddrinfo(results);
        return 2;
    }
    // Now we allow our port to listen
    if ((listen(server_fd, 10)) == -1)
    {
        perror("listen");
        exit(1);
    }
    freeaddrinfo(results); // free up the linked list
    cout << "Server is listening on port 8080..." << endl;
    //  Now Receive calls from clients
    //  declare & initilize needed vars
    sockaddr_storage client_addr;
    socklen_t addr_size = sizeof(client_addr);
    int client_fd;
    while (true)
    {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_size);
        if (client_fd == -1)
        {
            perror("accept");
            continue;
        }
        cout << "Client connected successfully.\n";
        //  ------Receive-Data---------
        int buffer_len, bytes_recv = 0;
        char buffer[1024];
        memset(buffer, 0, 1024);
        buffer_len = sizeof(buffer);
        bytes_recv += (recv(client_fd, buffer, buffer_len, 0));
        cout << bytes_recv << " bytes received.\n";
        if (bytes_recv > 0)
        {
            cout << bytes_recv << " bytes received.\n";
            cout << "Request data: \n"
                 << buffer << endl; // See what the browser sent
        }
        //
        const char *send_data =
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 14\r\nConnection: close\r\n\r\nHusnain here !";
        int len, bytes_sent;
        len = strlen(send_data);
        bytes_sent = send(client_fd, send_data, len, 0);

        close(client_fd); // Close the connection after sending
    }
    return 0;
}