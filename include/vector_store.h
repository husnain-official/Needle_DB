#ifndef VECTOR_STORE
#define VECTOR_STORE
#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <cmath>     // for sqrt
#include <algorithm> // for std::sort and std::partial sort
#include <sstream>   // for parsing logic of commands(  ss not used can be removed, confirm later)
#include <numeric>   // for std::inner_product (highly optimised dot-product)
#include <stdexcept> // for thorw-catch blocks in parsing logic
#include <cstdint>   // for uint64_t
//--Constants   *POINT WHERE PROJECT INITIAL-CONDITIONS ARE SET*
constexpr size_t dimensions_no_of_digits = 4;
constexpr size_t dimensions_set = 1024;
constexpr size_t id_length_set = 32;
//--Data-Structures
struct Vector
{
    std::string id;
    int dims;
    // std::map<std::string, std::string> metadata;
    std::vector<float> data;
};
struct Parse_result // Helper to identify success/failure of a function call/parse
{
    bool success;
    std::string message; // Error if failed.
};
struct Query_result // for vector_server use(only)
{
    float similarity;
    std::size_t index; // directly realted to database indexes
                       // to eaily sort the similar vectors
    bool operator>(const Query_result &other) const
    {
        return (this->similarity > other.similarity);
    }
};
//--Similarity functions
float cosine_similarity(const std::vector<float> &, const std::vector<float> &);
float cosine_similarity(const std::vector<float> &, const float *);
float dot_similarity(const std::vector<float> &, const std::vector<float> &);
float dot_similarity(const std::vector<float> &, const float *);
// --Vector-store class
class Vector_store
{
    std::vector<std::string> ids_;
    std::vector<float> embeddings_;
    // std::vector<std::map<std::string, std::string>> metadata_;
    std::size_t dims_;
    std::size_t count_;

public:
    Vector_store() : dims_(dimensions_set), count_(0) {}
    // Getters
    const float *get_embedding(size_t i) const;
    const std::string &get_id(size_t i) const;
    const std::size_t get_dims() const;
    const std::size_t get_count() const;
    Parse_result get_index_in_ram(const std::string &);

    // Setters
    Parse_result set_dims_(const std::size_t);
    Parse_result set_count_(const int);
    void clear();
    void make_entry(const std::string, std::vector<float>);
    bool remove_entry(const std::string &);
    bool insert(const Vector &);
    //
    bool normalise_vector(std::vector<float> &);
    bool read_all_ids(std::vector<std::string> &read_ids, const std::vector<std::size_t> &index, std::size_t &top_k);
    //  Search-functions
    std::vector<std::pair<std::string, float>> brute_force_search(const std::vector<float> &, const int);
    void return_k_most_similar(const Vector &, size_t &, std::vector<std::size_t> &, std::vector<float> &);
};
//----------------------------Parsing For 'Vector_Server' ----------------------------------
Parse_result insert_parsing(Vector &, const std::string &);
Parse_result query_parsing(Vector &, size_t &, const std::string &);
Parse_result delete_parsing(std::string &, const std::string &);
Parse_result save_parsing(std::string &, bool);
//--helpers
void next_space_changes(const std::string &, const std::size_t &, std::size_t &, std::size_t &);
#endif