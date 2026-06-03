#include "vector_server.h"
// Open 'Beejs guide to Network Programming', to understand this easily.
// https://beej.us/guide/bgnet/html/split/system-calls-or-bust.html#getaddrinfoprepare-to-launch
//  --- Setup
Vector_Server::Vector_Server(std::string port, Vector_store &V_store, File_manager &F_manager, const Config conditions) : port_num(port), server_fd(-1), vector_store(V_store), file_manager(F_manager), con(conditions) {}
// set server_fd to -1, as a socket has not been assigned yet
Vector_Server::~Vector_Server() { stop(); }
bool Vector_Server::setup()
{
    // setup (necessary info for socket creation)
    addrinfo hints, *results, *node;
    int status = 0;
    memset(&hints, 0, sizeof(hints)); // Ensure 'hints' is empty
    hints.ai_family = AF_UNSPEC;      // AF_UNSPEC;      // Accept either 'IPv4' or 'IPv6'
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
//  --- Working
void Vector_Server::stop()
{
    if (server_fd != -1)
        close(server_fd);
}
void Vector_Server::run()
{
    // AUTO-LOAD on startup, Read the data from the database and load into RAM
    Header h = file_manager.read_header();
    if (h.total_vector_count > 0)
    {
        Parse_result res = vector_store.set_dims_(h.dimensions);
        if (res.success)
        {
            vector_store.clear(); // reset count_(RAM) to 0, clear arrays
            std::string id_buf;
            Metadata_entry mdata_arr[3];
            std::vector<float> embd_buf(h.dimensions);          // use h.dimensions
            for (uint64_t i = 0; i < h.total_vector_count; i++) // loop over records
            {
                if (!file_manager.read_vector(i, id_buf, embd_buf.data(), mdata_arr))
                    continue;                                         // skip deleted (flag=0) — make_entry won't count these
                vector_store.make_entry(id_buf, embd_buf, mdata_arr); // increments count_ itself
            }
            std::cout << "Auto-loaded " << vector_store.get_count()
                      << " vectors (" << h.dimensions << " dims) from disk.\n";
            // prints LIVE vector count, not total-including-deleted
        }
        else
        {
            std::cerr << "ERROR: Failed to set dimensions from database header.\n";
        }
    }
    else
    {
        std::cout << "No data in database found, starting fresh.\n";
    }
    // Now we first crete out IVF centroids
    vector_store.attach_index(&ivf_index_);
    ivf_index_.build_(vector_store);
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
    int buffer_len = 16384; // one entry is approximately 4kB
    int64_t bytes_recv = 0;
    char buffer[buffer_len];
    std::string accumulator; // will accumulate the buffer over all recv calls unitl a '\n'
    while (true)
    {
        memset(buffer, 0, buffer_len);
        bytes_recv = (recv(client_fd, buffer, buffer_len - 1, 0));
        // std::cout << buffer;
        std::string command;
        // safety-check
        if (bytes_recv <= 0)
        {
            if (!accumulator.empty())
            {
                std::cout << "Warning: Client disconnected, but left incomplete data: " << std::endl;
                //   << accumulator << std::endl;
            }
            break;
        } // client disconnected
        accumulator.append(buffer, bytes_recv); // append exactly bytes_recv, not until \0
                                                // Process all complete commands in the accumulator
        std::size_t newline_pos;
        while ((newline_pos = accumulator.find('\n')) != std::string::npos)
        {
            command = accumulator.substr(0, newline_pos);
            accumulator.erase(0, newline_pos + 1); // consume this command

            // Trim \r if present (Windows clients send \r\n)
            if (!command.empty() && command.back() == '\r')
                command.pop_back();

            if (command.empty())
                continue;

            // std::cout << "Received: " << command << std::endl;
            //------------All Commands Conditionals-----------------
            if ((command.rfind("INSERT", 0)) == 0) // INSERT ID DIMS key1=abc key2=def key3=xyz F1 F2 F3 ... Fn
            {
                Vector v;
                Parse_result results = insert_parsing(v, command, con);
                if (!results.success)
                {
                    send(client_fd, results.message.data(), results.message.length(), 0);
                    continue;
                }
                if (vector_store.id_exists(v.id))
                {
                    results.message = "WARNING <Id already exists in database>\n";
                    send(client_fd, results.message.data(), results.message.length(), 0);
                    continue;
                }
                if (!vector_store.normalise_vector(v.data))
                {
                    results.message = "ERROR <Vector Normalization Failed>\n";
                    send(client_fd, results.message.data(), results.message.length(), 0);
                    continue;
                }
                if (!file_manager.write_vector(v.id, v.data.data(), v.metadata))
                {
                    results.message = "ERROR <Vector Writing Failed>\n";
                    send(client_fd, results.message.data(), results.message.length(), 0);
                    continue;
                }
                vector_store.make_entry(v.id, v.data, v.metadata); // update the RAM as well.
                // Notify IVF of the new entry (count_ just incremented, so index = count-1)
                // if (vector_store.get_count() > 0)
                //     ivf_index_.add_(vector_store.get_count() - 1);
                results.message = "INSERT <Successful>\n";
                results.message = "OK\n";
                send(client_fd, results.message.data(), results.message.length(), 0);
                continue;
            }
            else if ((command.rfind("QUERY", 0)) == 0) // QUERY TOP-K DIMS key1=abc key2=def key3=xyz F1 F2 ... Fn
            {
                Vector query_v;
                size_t top_k = 0;
                Parse_result results = query_parsing(query_v, top_k, command, con);
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
                //
                std::vector<size_t> matching_index;
                results = vector_store.get_matching_indices(query_v.metadata, matching_index);
                if (!results.success)
                {
                    send(client_fd, results.message.data(), results.message.length(), 0);
                    continue;
                }
                // 0. setup necessary variables
                std::vector<std::size_t> index;
                index.reserve(top_k);
                std::vector<std::string> id;
                std::vector<float> similarities;
                similarities.reserve(top_k); // push_back now fills from position 0
                // 1. Use ivf, if failed fallback to brute force
                std::vector<size_t> ivf_candidates = ivf_index_.search_(query_v, top_k);
                if (!ivf_candidates.empty())
                {
                    // Intersect ivf_candidates with matching_index (metadata filter)
                    std::vector<size_t> filtered;
                    for (size_t c : ivf_candidates)
                    {
                        if (matching_index.empty() ||
                            std::find(matching_index.begin(), matching_index.end(), c) != matching_index.end())
                            filtered.push_back(c);
                    }
                    // Use filtered as the candidate pool — pass as matching_index override
                    vector_store.return_k_most_similar(query_v, top_k, index, similarities, filtered.empty() ? &matching_index : &filtered);
                }
                else
                    vector_store.return_k_most_similar(query_v, top_k, index, similarities, &matching_index);
                // now return the id's of top_k similar vectors
                results.message = ("QUERY <" + std::to_string(top_k) + ">\n");
                send(client_fd, results.message.data(), results.message.length(), 0);
                vector_store.read_all_ids(id, index, top_k);
                for (size_t i = 0; i < top_k; i++) // display each output FORMAT: id score\n
                {
                    results.message = id[i] + " " + std::to_string(similarities[i]) + "\n";
                    send(client_fd, results.message.data(), results.message.length(), 0);
                }
                send(client_fd, "END\n", 4, 0);
                continue;
            }
            else if ((command.rfind("DELETE", 0)) == 0) // DELETE ID_NAME
            {
                std::string id = "";
                Parse_result results;
                results = delete_parsing(id, command, con);
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
                    results.message = "ERROR <Could not delete vector(Database)\n>";
                    send(client_fd, results.message.data(), results.message.length(), 0);
                    continue;
                }
                // Get RAM index before removing (needed for IVF)
                Parse_result idx_res = vector_store.get_index_in_ram(id);
                size_t ram_idx = idx_res.success ? std::stoul(idx_res.message) : SIZE_MAX;
                if (!vector_store.remove_entry(id))
                {
                    results.message = "ERROR <Could not delete vector(Memory)\n>";
                    send(client_fd, results.message.data(), results.message.length(), 0);
                    continue;
                }
                if (ram_idx != SIZE_MAX)
                    ivf_index_.delete_(ram_idx);
                results.message = "DELETE <Successful>\n";
                results.message = "OK\n";
                send(client_fd, results.message.data(), results.message.length(), 0);
            }
            else if ((command.rfind("SAVE", 0)) == 0) // SAVE
            {
                Parse_result results;
                results = save_parsing(command, 0, con);
                if (!results.success)
                {
                    send(client_fd, results.message.data(), results.message.length(), 0);
                    continue;
                }
                results.message = "SAVE <Successful>\n";
                results.message = "OK\n";
                file_manager.flush_header();
                send(client_fd, results.message.data(), results.message.length(), 0);
                continue;
            }
            else if ((command.rfind("LOAD", 0)) == 0) // LOAD
            {
                Parse_result results;
                results = save_parsing(command, 1, con);
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
                    vector_store.clear();
                    // read and write
                    std::string id_buf;
                    std::vector<float> embd_buf(vector_store.get_dims());
                    Metadata_entry mdata_arr[h.max_kv];
                    for (uint64_t i = 0; i < file_manager.get_total_vector_count(); i++)
                    {
                        if (!file_manager.read_vector(i, id_buf, embd_buf.data(), mdata_arr))
                            continue;                                         // skip deleted (flag=0) or bad records
                        vector_store.make_entry(id_buf, embd_buf, mdata_arr); // this increments count itself
                    }
                    ivf_index_.build_(vector_store);
                }
                results.message = "LOAD <Successful>\n";
                results.message = "OK\n";
                send(client_fd, results.message.data(), results.message.length(), 0);
                continue;
            } // 'load' end
            // std::cout << "\nEnd of inner loop, command does not match\n";
        } // inner loop end
        // std::cout << "\nEnd of outer loop, no'\n'yet\n";
    } // outer loop end

    close(client_fd); // Close the connection after sending
    std::cout << "Client disconnected." << std::endl;
}
