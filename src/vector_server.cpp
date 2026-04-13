#include "vector_server.h"
// Open 'Beejs guide to Network Programming', to understand this easily
// https://beej.us/guide/bgnet/html/split/system-calls-or-bust.html#getaddrinfoprepare-to-launch
//  The setup
Vector_Server::Vector_Server(std::string port, const Vector_store &V_store, const File_manager &F_manager) : port_num(port), server_fd(-1), vector_store(V_store), file_manager(F_manager)
// set server_fd to -1, as a socket has not been assigned yet
{
}
Vector_Server::~Vector_Server() { stop(); } //
bool Vector_Server::setup()
{
    // setup (necessary info for socket creation)
    addrinfo hints, *results, *node;
    int status = 0;
    memset(&hints, 0, sizeof(hints)); // Ensure 'hints' is empty
    hints.ai_family = AF_UNSPEC;      // Accept either 'IPv4' or 'IPv6'
    hints.ai_socktype = SOCK_STREAM;  // Sock-Stream will be used -> 2 way connection needed
    hints.ai_flags = AI_PASSIVE;      // Fill up my Ip address, (I am the server)
    // Safety-Check
    if ((status = getaddrinfo(NULL, port_num.c_str(), &hints, &results)) != 0)
    // basically, 'getaddrinfo' returns a status and a refrence to linked list with all possible sockets which match our requirements'hints'
    {
        // 'cerr' if error, show it immediately. we use gai_strerror as it has its unique error codes
        std::cerr << gai_strerror(status);
        return false;
    }
    // Find a valid socket and bind to it
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
        server_fd = -1; // Reset if bind failed
    }
    if (node == NULL)
    {
        // we looped through everything and nothing worked, sad life :(
        std::cerr << "Failed to bind to any address" << std::endl;
        freeaddrinfo(results);
        return false;
    }
    freeaddrinfo(results);
    return true;
}
//  The working
void Vector_Server::stop()
{
    if (server_fd != -1)
        close(server_fd);
}
void Vector_Server::run()
{
    // Now we allow our port to listen
    if ((listen(server_fd, BACKLOG)) == -1)
    {
        perror("listen");
        exit(1);
    }
    std::cout << "Server is listening on port " << Vector_Server::port_num << "..." << std::endl;
    //  Now Receive calls from clients
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
        std::cout << "Client connected successfully.\n";
        handle_client(client_fd);
    }
}
void Vector_Server::handle_client(int client_fd)
{
    int buffer_len = 4096, bytes_recv = 0;
    char buffer[buffer_len];
    while (true)
    {
        memset(buffer, 0, buffer_len);
        bytes_recv = (recv(client_fd, buffer, buffer_len - 1, 0));
        std::string command(buffer);
        // safety-check
        if (bytes_recv <= 0)
            break; // client disconnected
        std::cout << "Received: " << command << std::endl;
        //------------All Commands Conditionals-----------------
        if ((command.rfind("INSERT", 0)) == 0) // INSERT ID_NAME DIMS F1 F2 F3 ... Fn
        {
            Vector v;
            if (insert_parsing(v, command))
            {
                // save to file
                std::cout << "Ok\n";
            }
            continue;
        }
        else if ((command.rfind("QUERY", 0)) == 0) // QUERY TOP-K DIMS F1 F2 ... Fn
        {
            Vector v;
            size_t top_k = 0;
            if (query_parsing(v, top_k, command))
            {
                // do whatever
            }
            continue;
        }
        else if ((command.rfind("DELETE", 0)) == 0)
        {
            // do delete things
        }
        else if ((command.rfind("SAVE", 0)) == 0)
        {
            // do save things
        }
        else if ((command.rfind("LOAD", 0)) == 0)
        {
            // do load things
        }
        //
        close(client_fd); // Close the connection after sending
    }
}