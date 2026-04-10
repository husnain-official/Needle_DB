#include <iostream>
#include <fstream>
#include <cassert>
#include <cmath>
#include "vector_store.h"
#include "nlohmann/json.hpp"
#include "file_manager.h"
#include "../tests/file_test.h"
using namespace std;
int main()
{
    // ------------------TESTING-SEMANTIC-SIMILARITIES----------------------
    // {
    // Test 1: identical vectors must score 1.0
    // std::vector<float> a = {1.00, 0.00};
    // std::vector<float> b = {1.00, 0.00};
    // assert(cosine_similarity(a, b) == 1.00f);
    // // Test 2: opposite vectors must score -1.0
    // std::vector<float> c = {1.00, 0.00};
    // std::vector<float> d = {-1.00, 0.00};
    // assert(cosine_similarity(c, d) == -1.00f);
    // // Test 3: perpendicular vectors must score 0.0
    // std::vector<float> e = {1.00, 0.00};
    // std::vector<float> f = {0.00, 1.00};
    // assert(cosine_similarity(e, f) == 0.00f);
    // // Test 4:  with generated embeddings
    // std::ifstream file("tests/test_vectors.json");
    // if (!file.is_open())
    // {
    //     std::cerr << "ERROR: Could not open test_vectors.json - check the path!" << std::endl;
    //     return 1;
    // }
    // nlohmann::json j;
    // file >> j;
    // Vector_store vs;
    // for (auto &item : j)
    // {
    //     Vector v;
    //     v.id = item["id"];
    //     v.dims = item["dims"];
    //     v.data = item["data"].get<std::vector<float>>();
    //     vs.insert(v);
    // }
    // // use chunk_01's vector as the query — it should return itself as top result
    // auto results = vs.brute_force_search(vs.get_vector_data(0), 10);
    // for (auto &[id, score] : results)
    // {
    //     std::cout << id << "  →  " << score << "\n";
    // }
    // }

    //-------------------------TESTING-DATABASE-------------------------------
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