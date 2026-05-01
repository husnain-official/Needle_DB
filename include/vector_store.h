#ifndef VECTOR_STORE
#define VECTOR_STORE
#include <vector>
#include <string>
#include <cstring>
#include <map>
#include <iostream>
#include <cmath>     // for sqrt
#include <algorithm> // for std::sort and std::partial sort
#include <numeric>   // for std::inner_product (highly optimised dot-product)
#include <stdexcept> // for thorw-catch blocks in parsing logic
#include <cstdint>   // for uint64_t
#include "types.h"   // for convinient structs
//--Constants   *POINT WHERE PROJECT INITIAL-CONDITIONS ARE SET*
constexpr size_t dimensions_no_of_digits = 4;
constexpr size_t dimensions_set = 1536;
// constexpr size_t dimensions_set = 1024;
constexpr size_t id_length_set = 32;
constexpr size_t meta_data_length_set = 32;
constexpr size_t meta_data_kp_pairs_set = 3;

// --Vector-store class
class Vector_store
{
    std::vector<std::string> ids_;
    std::vector<float> embeddings_;
    std::map<std::string, std::map<std::string, std::string>> metadata_;
    std::size_t dims_;
    std::size_t count_;

public:
    Vector_store() : dims_(dimensions_set), count_(0) {}
    // Getters
    const float *get_embedding(size_t i) const;
    const std::string &get_id(size_t i) const;
    const std::size_t get_dims() const;
    const std::size_t get_count() const;
    Parse_result get_metadata_entry() const;
    Parse_result get_index_in_ram(const std::string &);
    Parse_result get_matching_indices(const Metadata_entry *, std::vector<size_t> &);

    // Setters
    Parse_result set_dims_(const std::size_t);
    Parse_result set_count_(const int);
    void set_metadata(const Metadata_entry *, const std::string &id);
    void clear();
    void make_entry(const std::string, std::vector<float>, const Metadata_entry *mdata_arr);
    bool remove_entry(const std::string &);
    //
    bool normalise_vector(std::vector<float> &);
    bool read_all_ids(std::vector<std::string> &read_ids, const std::vector<std::size_t> &index, std::size_t &top_k);
    //  Search-functions
    std::vector<std::pair<std::string, float>> brute_force_search(const std::vector<float> &, const int);
    void return_k_most_similar(const Vector &, size_t &, std::vector<std::size_t> &, std::vector<float> &, std::vector<size_t> *selected_indexes = nullptr);
};
#endif
/*
        1.  i am not sure if i should change std::map metadata to a unordered_map
        2.  removed 'insert' it was a full copy of 'make_entry'
        3.  isnt cosine similarity completely useless now, why do i even have it in the code still ?
        4.
*/
