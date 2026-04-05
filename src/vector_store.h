#include <vector>
#include <string>
#ifndef VECTOR_STORE
#define VECTOR_STORE
struct Vector
{
    std::string id;
    int dims;
    std::vector<float> data;
};
float cosine_similarity(const std::vector<float> &, const std::vector<float> &);
class Vector_store
{
    std::vector<Vector> store;

public:
    void insert(const Vector &);
    std::vector<float> &get_vector_data(size_t);
    std::vector<std::pair<std::string, float>> brute_force_search(const std::vector<float> &, const int);
};
#endif