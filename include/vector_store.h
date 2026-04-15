#ifndef VECTOR_STORE
#define VECTOR_STORE
#include <vector>
#include <string>
#include <iostream>
#include <math.h>
#include <algorithm>
#include <sstream>
//
#define dimensions_no_of_digits (4)
#define dimensions_set (1056)
#define id_length_set (32)
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
    bool normalise_vector(std::vector<float> &vec);
};
//----------------------------Functionality For 'Vector_Server'----------------------------------
bool insert_parsing(Vector &, const std::string &);
bool query_parsing(Vector &, size_t &, const std::string &);
bool delete_parsing(std::string &, const std::string &);
bool save_parsing(std::string &, bool);
//----------helpers----------------
void next_space_changes(const std::string &, const std::size_t &, std::size_t &, std::size_t &);
#endif