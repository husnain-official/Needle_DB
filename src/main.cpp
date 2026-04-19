#include "vector_store.h"
#include "file_manager.h"
#include "vector_server.h"
#include "../tests/integration/json.hpp"
using namespace std;
int main()
{
    {
        cout << "VectorDB   starting...\n";
        // ----------------------Declare All Components-------------------------
        Vector_store database;
        //
        std::string path = "data/database.vdb";
        File_manager file_handler(path, dimensions_set, id_length_set);
        //
        const char *SERVER_IP = "192.168.180.151";
        std::string port = "8080";
        Vector_Server server(port, database, file_handler);
        //----------------------------Main Body---------------------------------
        server.setup();
        server.run();
        server.stop();
        //----------------------------Program End-------------------------------
    }

    //
    // cout << "It works\n";
    // cout << "Id: " << id << endl;
    // cout << "Top_k: " << top_k << endl;
    // cout << "Dims: " << v.dims << endl;
    // cout << std::fixed << std::setprecision(5);
    // for (int i = 0; i < dimensions; i++)
    // {
    //     cout << "Data[" << i << "] is: " << v.data[i] << "      ";
    // }

    return 0;
}