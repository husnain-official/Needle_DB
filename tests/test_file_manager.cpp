#include <iostream>
#include <fstream>
#include <cassert>
#include <cmath>
#include "vector_store.h"
#include "json.hpp"
#include "file_manager.h"
#include "test_file_manager.h"
using namespace std;
int main()
{
    {
        const std::string BASE = "./test_db_";
        const uint32_t DIMS = 8; // tiny dims — fast, easy to reason about
        int failed = 0;

        // Each test gets its own fresh file
        auto run = [&](const char *tag, auto fn)
        {
            std::string p = BASE + tag + ".vdb";
            std::filesystem::remove(p); // always start clean
            if (!fn(p, DIMS))
                ++failed;
        };

        run("fresh", test_fresh_create);
        run("rw", test_write_and_read);
        run("persist", test_persistence);
        run("delete", test_delete_and_tombstone);
        run("find", test_find_by_id);
        run("vacuum", test_vacuum_fill);
        run("schema", test_schema_mismatch);
        run("truncate", test_id_truncation);

        std::cout << "\n────────────────────────────────\n";
        if (failed == 0)
            std::cout << "All tests passed.\n";
        else
            std::cout << failed << " test group(s) had failures.\n";

        return failed == 0 ? 0 : 1;
    }
    return 0;
}