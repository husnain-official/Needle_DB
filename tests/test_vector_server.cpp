#include "vector_server.h"
#include <iostream>
#include <vector>
#include <cmath> // For std::abs
#include "env_config.hpp"

int main()
{
    std::cout << "VectorDB   starting...\n";
    // -----------------------Configure with .env---------------------------
    Config env;
    bool success = loadServerConfig(".env", env);
    if (!success)
    {
        std::cout << "Error: Server could not read .env file properly\n";
        return -1;
    }
    // ----------------------Declare All Components-------------------------
    Vector_store database(env);
    //
    std::string path = "data/database.vdb";
    File_manager file_handler(path, env.dims, env.id_length, env.meta_data_length, env.meta_data_pairs);
    //
    std::string port = "8080";
    Vector_Server server(port, database, file_handler, env);
    //----------------------------Main Body---------------------------------
    server.setup();
    server.run();
    server.stop();

    return 0;
}