#include <iostream>
#include <fstream>
#include <cassert>
#include <cmath>
#include "vector_store.h"
#include "json.hpp"
#include "file_manager.h"
#include "test_file_manager.h"
#include "similarities.hpp"
#include "command_parser.h"
#include "types.h"
using namespace std;
int main()
{
    // Test 1 : identical vectors must score 1.0
    std::vector<float> a = {1.00, 0.00};
    std::vector<float> b = {1.00, 0.00};
    assert(cosine_similarity(a, b) == 1.00f);
    // Test 2: opposite vectors must score -1.0
    std::vector<float> c = {1.00, 0.00};
    std::vector<float> d = {-1.00, 0.00};
    assert(cosine_similarity(c, d) == -1.00f);
    // Test 3: perpendicular vectors must score 0.0
    std::vector<float> e = {1.00, 0.00};
    std::vector<float> f = {0.00, 1.00};
    assert(cosine_similarity(e, f) == 0.00f);
    // Test 4:  with generated embeddings
    std::ifstream file("tests/test_data/test_vectors.json");
    if (!file.is_open())
    {
        std::cerr << "ERROR: Could not open test_vectors.json - check the path!" << std::endl;
        return 1;
    }
    nlohmann::json j;
    file >> j;
    Vector_store vs;
    for (auto &item : j)
    {
        Vector v;
        v.id = item["id"];
        v.dims = item["dims"];
        v.data = item["data"].get<std::vector<float>>();
        // vs.insert(v);
    }
    // use chunk_01's vector as the query — it should return itself as top result
    // auto results = vs.brute_force_search(vs.get_vector_data(0), 10);
    // for (auto &[id, score] : results)
    // {
    //     std::cout << id << "  -> " << score << "\n";
    // }
    return 0;
}