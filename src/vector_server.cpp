#include "vector_server.h"
// Open 'Beejs guide to Network Programming', to understand this easily
// https://beej.us/guide/bgnet/html/split/system-calls-or-bust.html#getaddrinfoprepare-to-launch
//  The setup
Vector_Server::Vector_Server(std::string port, Vector_store &V_store, File_manager &F_manager) : port_num(port), server_fd(-1), vector_store(V_store), file_manager(F_manager)
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
    int buffer_len = 16384, bytes_recv = 0;
    char buffer[buffer_len];
    while (true)
    {
        memset(buffer, 0, buffer_len);
        bytes_recv = (recv(client_fd, buffer, buffer_len - 1, 0));
        std::string command(buffer);
        // safety-check
        if (bytes_recv <= 0)
            break; // client disconnected
        // Trim trailing whitespace, \n, and \r
        command.erase(command.find_last_not_of(" \n\r\t") + 1);
        std::cout << "Received: " << command << std::endl;
        //------------All Commands Conditionals-----------------
        if ((command.rfind("INSERT", 0)) == 0) // INSERT ID_NAME DIMS F1 F2 F3 ... Fn
        {
            Vector v;
            Parse_result results = insert_parsing(v, command);
            if (!results.success)
            {
                send(client_fd, results.message.data(), results.message.length(), 0);
                continue;
            }
            //
            if (!vector_store.normalise_vector(v.data))
            {
                results.message = "ERROR <Vector Normalization Failed>\n";
                send(client_fd, results.message.data(), results.message.length(), 0);
                continue;
            }
            if (!file_manager.write_vector(v.id, v.data.data()))
            {
                results.message = "ERROR <Vector Writing Failed>\n";
                send(client_fd, results.message.data(), results.message.length(), 0);
                continue;
            }
            results.message = "INSERT <Successful>\n";
            send(client_fd, results.message.data(), results.message.length(), 0);
            continue;
        }
        else if ((command.rfind("QUERY", 0)) == 0) // QUERY TOP-K DIMS F1 F2 ... Fn
        {
            Vector query_v;
            size_t top_k = 0;
            Parse_result results = query_parsing(query_v, top_k, command);
            if (!results.success)
            {
                send(client_fd, results.message.data(), results.message.length(), 0);
                continue;
            }
            if (!vector_store.normalise_vector(query_v.data))
            {
                results.message = "ERROR <Vector Normalization Failed>\n";
                send(client_fd, results.message.data(), results.message.length(), 0);
                continue;
            }
            std::vector<std::size_t> index(top_k);
            std::vector<std::string> id(top_k);
            std::vector<float> similarities(top_k);
            vector_store.return_k_most_similar(query_v, top_k, index, similarities);
            // now return the id's of top_k similar vectors
            results.message = "QUERY <TOP-K>\n";
            send(client_fd, results.message.data(), results.message.length(), 0);
            vector_store.read_all_ids(id, index, top_k);
            for (size_t i = 0; i < top_k; i++) // display each output FORMAT: id score\n
            {
                results.message = id[i] + std::to_string(similarities[i]) + "\n";
                send(client_fd, results.message.data(), results.message.length(), 0);
            }
            continue;
        }
        else if ((command.rfind("DELETE", 0)) == 0) // DELETE ID_NAME
        {
            std::string id = "";
            Parse_result results;
            results = delete_parsing(id, command);
            if (!results.success)
            {
                send(client_fd, results.message.data(), results.message.length(), 0);
                continue;
            }
            int64_t index = -1;
            index = file_manager.find_by_id(id);
            if (index == -1)
            {
                results.message = "ERROR <Could not find vector>\n";
                send(client_fd, results.message.data(), results.message.length(), 0);
                continue;
            }
            if (!file_manager.delete_vector(static_cast<uint64_t>(index)))
            {
                results.message = "ERROR <Could not delete vector\n>";
                send(client_fd, results.message.data(), results.message.length(), 0);
                continue;
            }
            results.message = "DELETE <Successful>\n";
            send(client_fd, results.message.data(), results.message.length(), 0);
        }
        else if ((command.rfind("SAVE", 0)) == 0) // SAVE
        {
            Parse_result results;
            results = save_parsing(command, 0);
            if (!results.success)
            {
                send(client_fd, results.message.data(), results.message.length(), 0);
                continue;
            }
            results.message = "SAVE <Successful>\n";
            file_manager.flush_header();
            send(client_fd, results.message.data(), results.message.length(), 0);
            continue;
        }
        else if ((command.rfind("LOAD", 0)) == 0) // LOAD
        {
            Parse_result results;
            results = save_parsing(command, 1);
            if (!results.success) // save and load -> 4 chars same logic
            {
                send(client_fd, results.message.data(), results.message.length(), 0);
                continue;
            }
            // do load things
            { // as this is the connection point for all three classes, code will be here
                Header h = file_manager.read_header();
                results = vector_store.set_dims_(h.dimensions);
                if (!results.success)
                {
                    send(client_fd, results.message.data(), results.message.length(), 0);
                    continue;
                }
                results = vector_store.set_count_(h.vector_count);
                if (!results.success)
                {
                    send(client_fd, results.message.data(), results.message.length(), 0);
                    continue;
                }
                // pre-allocate to avoid repeated reallocations
                vector_store.clear();
                // read and write
                std::string id_buf;
                std::vector<float> embd_buf(vector_store.get_dims());
                for (uint64_t i = 0; i < vector_store.get_dims(); i++)
                {
                    if (!file_manager.read_vector(i, id_buf, embd_buf.data()))
                        continue; // skip deleted (flag=0) or bad records
                    vector_store.make_entry(id_buf, embd_buf);
                }
            }
            results.message = "LOAD <Successful>\n";
            send(client_fd, results.message.data(), results.message.length(), 0);
            continue;
        }
        //
        close(client_fd); // Close the connection after sending
    }
}
// testing
void Vector_Server::test_read_write(File_manager &file_handler)
{
    std::cout << "\n--- RUNNING ISOLATED DB TEST ---\n";

    // 1. Create a completely unique, predictable test vector
    std::string test_id = "Isolated-777";
    std::vector<float> test_embds(1056);
    for (int i = 0; i < 1056; i++)
    {
        test_embds[i] = (float)i * 0.5f; // Generates: 0.0, 0.5, 1.0, 1.5...
    }

    // 2. Get the exact index where this new vector is ABOUT to be saved
    uint64_t target_index = file_handler.vector_count();

    // 3. Insert it directly into the database
    bool write_success = file_handler.write_vector(test_id, test_embds.data());
    if (!write_success)
    {
        std::cout << "❌ TEST FAILED: Could not write to file.\n";
        return;
    }

    // 4. Read it back from that EXACT index
    std::string retrieved_id = "";
    std::vector<float> retrieved_embds(1056, 0);
    bool read_success = file_handler.read_vector(target_index, retrieved_id, retrieved_embds.data());

    if (!read_success)
    {
        std::cout << "❌ TEST FAILED: Could not read from index " << target_index << ".\n";
        return;
    }

    // 5. Compare Results
    if (retrieved_id != test_id)
    {
        std::cout << "❌ TEST FAILED: ID mismatch! Expected '" << test_id << "', got '" << retrieved_id << "'\n";
        return;
    }

    int error_count = 0;
    for (size_t i = 0; i < 5; i++)
    { // Check the first 5 floats
        if (std::abs(test_embds[i] - retrieved_embds[i]) > 1e-5)
        {
            std::cout << "❌ TEST FAILED at dim " << i
                      << "! Expected: " << test_embds[i]
                      << " | Got: " << retrieved_embds[i] << "\n";
            error_count++;
        }
    }

    if (error_count == 0)
    {
        std::cout << "✅ ISOLATED TEST PASSED! The File_manager is reading and writing perfectly.\n";
    }
}