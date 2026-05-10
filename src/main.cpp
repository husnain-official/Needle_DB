#include "vector_store.h"
#include "file_manager.h"
#include "vector_server.h"
using namespace std;
int main()
{
        cout << "VectorDB   starting...\n";
        // ----------------------Declare All Components-------------------------
        Vector_store database;
        // Configure Database
        std::string path = "data/database.vdb";
        File_manager file_handler(path, dimensions_set, id_length_set, meta_data_length_set, meta_data_kp_pairs_set);
        // Configure Server
        std::string port = "8080";
        Vector_Server server(port, database, file_handler);
        //----------------------------Main Body---------------------------------
        server.setup();
        server.run();
        server.stop();
        //----------------------------Program End-------------------------------

    return 0;
}