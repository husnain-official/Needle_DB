#ifndef VECTOR_STORE
#define VECTOR_STORE
#include <vector>
#include <string>
#include <cstring>
#include <map>
#include <iostream>
#include <cmath>            // for sqrt
#include <algorithm>        // for std::sort and std::partial sort
#include <numeric>          // for std::inner_product (highly optimised dot-product)
#include <stdexcept>        // for thorw-catch blocks in parsing logic
#include <cstdint>          // for uint64_t
#include "types.h"          // for convinient structs
#include "similarities.hpp" // for similarity functions
#include "env_config.hpp"
// #include "ivf.h"            // for ivf data

// --Index-Creation
class Vector_index;

// --Vector-store class
class Vector_store
{
    std::vector<std::string> ids_;
    std::vector<float> embeddings_;
    std::map<std::string, std::map<std::string, std::string>> metadata_;
    std::size_t dims_;
    std::size_t count_;
    Vector_index *index_ = nullptr;
    // THe pre-set configrations from .env
    const Config conditions;

public:
    // Constructor/Destructor
    Vector_store(const Config con) : conditions(con), dims_(con.dims), count_(0) {}
    ~Vector_store() {}
    // Getters
    const float *get_embedding(size_t i) const;
    const std::string &get_id(size_t i) const;
    const std::size_t get_dims() const;
    const std::size_t get_count() const;
    Parse_result get_metadata_entry() const;
    Parse_result get_index_in_ram(const std::string &);
    Parse_result get_matching_indices(const Metadata_entry *, std::vector<size_t> &);
    Vector_index *get_index() const;
    // Setters
    Parse_result set_dims_(const std::size_t);
    Parse_result set_count_(const int);
    void set_metadata(const Metadata_entry *, const std::string &);
    void clear();
    void make_entry(const std::string, std::vector<float>, const Metadata_entry *);
    bool remove_entry(const std::string &);
    void attach_index(Vector_index *);

    // Core-Functions
    bool normalise_vector(std::vector<float> &);
    bool read_all_ids(std::vector<std::string> &, const std::vector<std::size_t> &, std::size_t &);

    //  Search-functions
    std::vector<std::pair<std::string, float>> brute_force_search(const std::vector<float> &, const int);
    void return_k_most_similar(const Vector &, size_t &, std::vector<std::size_t> &, std::vector<float> &, std::vector<size_t> * = nullptr);
};
#endif