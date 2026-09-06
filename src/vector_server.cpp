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
    DB_header h = file_manager.read_header();
    if (h.total_vector_count > 0)
    {
        Parse_result res = vector_store.set_dims_(h.dimensions);
        if (res.success)
        {
            vector_store.clear(); // reset count_(RAM) to 0, clear arrays
            std::string temp_text_str;
            DB_entry db_entry;
            Vector vector_store_entry;

            for (uint64_t i = 0; i < h.total_vector_count; i++) // loop over records    // TODO: Specify who is responsible for padding
            {
                if (!file_manager.read_entry(i, db_entry, temp_text_str))
                    continue; // skip deleted (flag=0) — make_entry won't count these
                entry_to_vector(db_entry, vector_store_entry);
                vector_store.make_entry(vector_store_entry); // increments count_ itself
            }
            std::cout << "Auto-loaded " << vector_store.get_count()
                      << " live vectors (" << h.dimensions << " dims) from disk.\n";
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
    // Now we first crete out IVF centroids // TODO: In v2, the prebuilt centroids are to be stored in a file, and not created after every bootup
    vector_store.attach_index(&ivf_index_);
    if (!file_manager.is_index_populated())
    {
        ivf_index_.build_(vector_store);
        size_t centroids_to_save = std::min(size_t(schema::MAX_CENTROIDS), ivf_index_.get_built_centroids_number_());
        const float *centroids_ptr = ivf_index_.get_centroids_data_ptr_();
        file_manager.write_index_(centroids_ptr, centroids_to_save);
    }
    else
    {
        // new logic comes here now.
        // first we set the store, in the index file.
        ivf_index_.set_ref_store(vector_store);
        size_t centroids_to_copy = file_manager.get_index_size() / (schema::DIMENSIONS * sizeof(float));
        if (centroids_to_copy == 0) // if the index file size is 0, we rebuild, file exists with no data.
        {
            std::cerr << "[Server]  |   WARNING: Index file present but unusable -- rebuilding from scratch.\n";
            ivf_index_.build_(vector_store);
            size_t centroids_to_save = ivf_index_.get_built_centroids_number_();
            file_manager.write_index_(ivf_index_.get_centroids_data_ptr_(), centroids_to_save);
        }
        else // valid data inside index_ file.
        {
            ivf_index_.set_centroids(file_manager.read_index_(centroids_to_copy));
            // centroids have been set now, the indexes have to be assigned(lists have to be created)
            ivf_index_.build_lists();
        }
    }
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
        // If we have multiple accept's then we can have multiple handle_clients().
        // 1.   |   Main-Thereaded-Accept
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_size);
        if (client_fd == -1)
        {
            perror("accept");
            continue;
        }
        std::cout << "Client connected successfully.\n";
        // 2.   |   Per-Client-Thread
        // handle_client(client_fd);
        std::thread client_thread(&Vector_Server::handle_client, this, client_fd);
        // 3.   |   Detach the client thread from the main-thread
        client_thread.detach();
    }
}
void Vector_Server::handle_client(int client_fd)
{
    int buffer_len = 16384; // one entry is approximately 5.4kB
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
            if ((command.rfind("INSERT", 0)) == 0) // INSERT <id> <text_length> <text> <dims> [key=val ...] f1 f2 ... fn
            {
                bool insert_failed = false;
                std::string error_message;

                Vector vector_entry;
                DB_entry entry;
                std::string text;

                Parse_result results = parser.insert_parsing(entry, text, command);
                if (!results.success)
                {
                    send(client_fd, results.message.data(), results.message.length(), 0);
                    continue;
                }
                // ---- everything below reads or writes vector_store / file_manager, needs a lock ----
                {
                    std::lock_guard<std::mutex> lock(store_mutex_);

                    if (vector_store.id_exists(entry.id))
                    {
                        error_message = "WARNING <Id already exists in database>\n";
                        insert_failed = true;
                    }
                    else if (!vector_store.normalise_vector(entry.embeddings))
                    {
                        error_message = "ERROR <Vector Normalization Failed>\n";
                        insert_failed = true;
                    }
                    else if (!file_manager.write_entry(entry, text))
                    {
                        error_message = "ERROR <Entry Writing Failed>\n";
                        insert_failed = true;
                    }
                    else
                    {
                        if (!entry_to_vector(entry, vector_entry))
                        {
                            error_message = "ERROR <'DB_entry' to 'Vector' conversion Failed>\n";
                            insert_failed = true;
                        }
                        else
                        {
                            vector_store.make_entry(vector_entry); // update both RAM and  index_(internally).
                        }
                    }
                } // --- lock-ends-here ---, NOTE: .send() is not shared, therefore 1 single client conncection will not slow the entire server.
                if (insert_failed)
                {
                    send(client_fd, error_message.data(), error_message.length(), 0);
                    continue;
                }
                results.message = "INSERT <Successful>\n";
                send(client_fd, results.message.data(), results.message.length(), 0);
                continue;
            }
            else if ((command.rfind("QUERY", 0)) == 0)
            {
                Vector query_v;
                size_t top_k = 0;
                Parse_result results = parser.query_parsing(query_v, top_k, command);
                if (!results.success)
                {
                    send(client_fd, results.message.data(), results.message.length(), 0);
                    continue;
                }

                // Note: to not make send() in a locked state we first copy all the data into local variables, local to all individual threads
                std::vector<std::string> id;
                std::vector<float> similarities;
                std::vector<std::string> texts;
                bool query_failed = false;
                std::string error_message;
                // ---- everything below reads or writes vector_store / file_manager, needs a lock ----
                {
                    std::lock_guard<std::mutex> lock(this->store_mutex_);

                    if (!vector_store.normalise_vector(query_v.embeddings))
                    {
                        error_message = "ERROR <Vector Normalization Failed>\n";
                        query_failed = true;
                    }
                    else
                    {
                        std::vector<size_t> matching_index;
                        results = vector_store.get_matching_indices(query_v.meta_data, query_v.meta_data_count, matching_index);
                        if (!results.success)
                        {
                            error_message = results.message;
                            query_failed = true;
                        }
                        else
                        {
                            std::vector<std::size_t> index;
                            index.reserve(top_k);
                            similarities.reserve(top_k);

                            std::vector<size_t> ivf_candidates = ivf_index_.search_(query_v, top_k);
                            if (!ivf_candidates.empty())
                            {
                                std::vector<size_t> filtered;
                                for (size_t c : ivf_candidates)
                                {
                                    if (matching_index.empty() or
                                        std::find(matching_index.begin(), matching_index.end(), c) != matching_index.end())
                                        filtered.push_back(c);
                                }
                                vector_store.return_k_most_similar(query_v, top_k, index, similarities, filtered.empty() ? &matching_index : &filtered);
                            }
                            else
                                vector_store.return_k_most_similar(query_v, top_k, index, similarities, &matching_index);

                            if (!vector_store.read_all_ids(id, index, top_k))
                            {
                                error_message = "ERROR <[Vector-Store] Failed to read similar id's.>\n";
                                query_failed = true;
                            }
                            else
                            {
                                texts.resize(top_k);
                                for (size_t i = 0; i < top_k; i++)
                                {
                                    size_t text_length = vector_store.get_text_length(index[i]);
                                    size_t text_offset = vector_store.get_text_offset(index[i]);
                                    if (!file_manager.read_text(text_length, text_offset, texts[i]))
                                        texts[i].clear(); // mark as unreadable, handle below
                                }
                            }
                        }
                    }
                } // --- lock ends here ---
                if (query_failed)
                {
                    send(client_fd, error_message.data(), error_message.length(), 0);
                    continue;
                }
                results.message = ("QUERY <" + std::to_string(top_k) + ">\n");
                send(client_fd, results.message.data(), results.message.length(), 0);

                for (size_t i = 0; i < top_k; i++)
                {
                    if (texts[i].empty()) // sentinel for read_text failure above
                    {
                        results.message = "ERROR <SKIPPING possible match, Flag mismatch of text and entry. '" + id[i] + "'>";
                        send(client_fd, results.message.data(), results.message.length(), 0);
                        continue;
                    }
                    results.message = id[i] + " " + std::to_string(similarities[i]) + " " + texts[i] + "\n";
                    send(client_fd, results.message.data(), results.message.length(), 0);
                }
                send(client_fd, "END\n", 4, 0);
                continue;
            }
            else if ((command.rfind("DELETE", 0)) == 0) // DELETE <id>
            {
                std::string error_message;
                bool delete_failed = false;

                std::string id = "";
                Parse_result results;

                results = parser.delete_parsing(id, command);
                if (!results.success)
                {
                    send(client_fd, results.message.data(), results.message.length(), 0);
                    continue;
                }
                // ---- everything below reads or writes vector_store / file_manager, needs a lock ----
                {
                    std::lock_guard<std::mutex> lock(store_mutex_);

                    int64_t index = -1;
                    index = file_manager.find_by_id(id);
                    if (index == -1)
                    {
                        error_message = "ERROR <Could not find vector to delete in Database>\n";
                        delete_failed = true;
                    }
                    else if (!file_manager.delete_entry(static_cast<uint64_t>(index)))
                    {
                        error_message = "ERROR <Could not delete vector(Database)\n>";
                        delete_failed = true;
                    }
                    else if (!vector_store.remove_entry(id))
                    {
                        error_message = "ERROR <Could not delete vector(Memory)\n>";
                        delete_failed = true;
                    }
                }
                if (delete_failed)
                {
                    send(client_fd, error_message.data(), error_message.length(), 0);
                    continue;
                }
                results.message = "DELETE <Successful>\n";
                send(client_fd, results.message.data(), results.message.length(), 0);
            }
            else if ((command.rfind("SAVE", 0)) == 0) // SAVE
            {
                Parse_result results;
                results = parser.save_parsing(command, 0);
                if (!results.success)
                {
                    send(client_fd, results.message.data(), results.message.length(), 0);
                    continue;
                }
                // ---- everything below reads or writes vector_store / file_manager, needs a lock ----
                {
                    std::lock_guard<std::mutex> lock(store_mutex_);
                    file_manager.flush_header();
                }
                results.message = "SAVE <Successful>\n";
                send(client_fd, results.message.data(), results.message.length(), 0);
                continue;
            }
            else if ((command.rfind("LOAD", 0)) == 0) // LOAD
            {
                std::string error_message;
                bool load_failed = false;
                Parse_result results;
                results = parser.save_parsing(command, 1);
                if (!results.success) // save and load -> 4 chars same logic
                {
                    send(client_fd, results.message.data(), results.message.length(), 0);
                    continue;
                }
                // ---- everything below reads or writes vector_store / file_manager, needs a lock ----
                {
                    std::lock_guard<std::mutex> lock(store_mutex_);

                    // DB_header h = file_manager.read_header();
                    results = vector_store.set_dims_(schema::DIMENSIONS);
                    if (!results.success)
                    {
                        error_message = results.message;
                        load_failed = true;
                    }
                    else
                    {
                        vector_store.clear();
                        // read and write
                        std::string text;
                        DB_entry entry;
                        Vector vec_entry;
                        for (uint64_t i = 0; i < file_manager.get_total_vector_count(); i++)
                        {
                            text.clear();
                            if (!file_manager.read_entry(i, entry, text))
                                continue;
                            entry_to_vector(entry, vec_entry);
                            vector_store.make_entry(vec_entry);
                        }
                        ivf_index_.build_(vector_store);
                    }
                }
                if (load_failed)
                {
                    send(client_fd, error_message.data(), error_message.length(), 0);
                    continue;
                }
                results.message = "LOAD <Successful>\n";
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
