#include "vector_store.h"
#include "file_manager.h"
#include "vector_server.h"
#include "env_config.hpp"
using namespace std;
int main()
{
    cout << "Needle_DB(Server) starting...\n";

    // -----------------------Configure with .env---------------------------
    Config env;
    bool success = loadServerConfig(".env", env);
    if (!success)
    {
        cout << "Error: Server could not read .env file properly\n";
        return -1;
    }
    // ----------------------Declare All Components-------------------------
    Vector_store database(env);
    // Configure Database
    File_manager file_handler(env.vecdb_file_path, env.dims, env.id_length, env.meta_data_length, env.meta_data_pairs);
    // Configure Server
    Vector_Server server(env.port, database, file_handler, env);

    //----------------------------Main Body---------------------------------
    server.setup();
    server.run();
    server.stop();

    //----------------------------Program End-------------------------------
    return 0;
}