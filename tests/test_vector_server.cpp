#include "vector_server.h"
#include <iostream>
#include <vector>
#include <cmath> // For std::abs

int main()
{
    std::cout << "VectorDB   starting...\n";
    // ----------------------Declare All Components-------------------------
    Vector_store database;
    //
    std::string path = "data/database.vdb";
    File_manager file_handler(path, dimensions_set, id_length_set, meta_data_length_set, meta_data_kp_pairs_set);
    //
    std::string port = "8080";
    Vector_Server server(port, database, file_handler);
    //----------------------------Main Body---------------------------------
    server.setup();
    server.run();
    server.stop();

    return 0;
}