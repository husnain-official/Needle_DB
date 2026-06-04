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
    /**
     * @brief Initializes the memory storage engine using immutable environment parameters.
     * @param con Loaded configuration struct dictating operational constraints
     */
    Vector_store(const Config con) : conditions(con), dims_(con.dims), count_(0) {}
    ~Vector_store() {}
    // Getters
    const float *get_embedding(size_t i) const;
    const std::string &get_id(size_t i) const;
    const std::size_t get_dims() const;
    const std::size_t get_count() const;
    Parse_result get_metadata_entry() const;
    /**
     * @brief Locates the internal flat array offset of a specific vector identifier.
     * @param id Unique 32-character maximum string identifier
     * @return Success state containing stringified integer index or an error message
     * @warning Executes an unoptimized linear O(N) scan across all stored IDs
     */
    Parse_result get_index_in_ram(const std::string &);
    /**
     * @brief Filters stored vectors against required key-value metadata pairs.
     * @param mdata_arr Array containing up to 3 metadata conditional constraints
     * @param matching_indices Output vector populated with indices satisfying all conditions
     * @return Success state indicating the outcome of the filtering operation
     * @note Empty query keys act as wildcards matching all active entries
     */
    Parse_result get_matching_indices(const Metadata_entry *, std::vector<size_t> &);
    Vector_index *get_index() const;
    // Setters
    /**
     * @brief Validates and assigns the runtime embedding dimensionality against the configuration.
     * @param dim Target dimensionality extracted from the persistent database file header
     * @return Success state indicating if parsed dimensions match the environment rules
     */
    Parse_result set_dims_(const std::size_t);
    /**
     * @brief Overrides the internal tracker for live vector counts.
     * @param count Integer representing total valid vectors loaded into memory
     * @return Success state failing if the provided count falls below zero
     * @warning Does not resize or allocate underlying physical storage arrays
     */
    Parse_result set_count_(const int);
    /**
     * @brief Maps parsed key-value entries to a specific vector identifier in memory.
     * @param mdata_arr Array of structured metadata entries to store
     * @param id Target vector identifier to associate with the structured data
     * @note Silently ignores and drops entries containing empty string keys
     */
    void set_metadata(const Metadata_entry *, const std::string &);
    /**
     * @brief Purges all vectors, identifiers, and metadata mapping from active memory.
     * @warning Does not touch or modify the persistent binary .vdb file
     */
    void clear();
    /**
     * @brief Appends a new vector record into the contiguous flat memory arrays.
     * @param id_buf Unique string identifier for the incoming vector
     * @param embd_buf Raw floating-point embedding sequence
     * @param mdata_arr Associated metadata key-value array
     * @note Automatically propagates the insertion to the attached IVF index
     * @warning Assumes duplicate ID validation has already occurred prior to invocation
     */
    void make_entry(const std::string, std::vector<float>, const Metadata_entry *);
    /**
     * @brief Executes an O(1) swap-and-pop deletion of a vector from memory.
     * @param id Target vector identifier to permanently remove
     * @return True if successful, false if the identifier is missing
     * @note Mutates internal array indexing, requiring strict IVF index synchronisation
     */
    bool remove_entry(const std::string &);
    /**
     * @brief Binds an external nearest-neighbour indexing structure to the storage engine.
     * @param idx Pointer to a fully constructed index instance
     */
    void attach_index(Vector_index *);

    // Core-Functions
    /**
     * @brief Scales a targeted floating-point vector to unit length in place.
     * @param vec Target vector requiring L2 normalisation
     * @return True upon success, false if processing a zero-magnitude null vector
     * @warning Mutates the provided vector directly without allocating a copy
     */
    bool normalise_vector(std::vector<float> &);
    /**
     * @brief Resolves raw integer memory indices back into their string identifiers.
     * @param read_ids Output array populated with the mapped string identifiers
     * @param index Input array of memory offsets generated by a search operation
     * @param top_k Maximum identifier count to extract, silently clamped to available indices
     * @return True on success, false if any requested index exceeds array bounds
     */
    bool read_all_ids(std::vector<std::string> &, const std::vector<std::size_t> &, std::size_t &);
    /**
     * @brief Verifies the presence of a specific identifier within loaded memory.
     * @param id_to_check Target string identifier for validation
     * @return True if located in the memory map, false otherwise
     * @warning Executes an unoptimized linear O(N) scan across the ID array
     */
    bool id_exists(std::string &);

    //  Search-functions
    /**
     * @brief Performs an exhaustive dot-product similarity sweep across all stored vectors.
     * @param query Normalized floating-point target embedding
     * @param top_n Number of highest scoring matches to retrieve
     * @return Ranked list of identifiers paired with their computed similarity scores
     * @note Bypasses the clustered IVF index completely
     */
    std::vector<std::pair<std::string, float>> brute_force_search(const std::vector<float> &, const int);
    /**
     * @brief Calculates and partial-sorts the highest similarity matches for a query.
     * @param query_v Complete query record including requested metadata constraints and embedding
     * @param top_k Target result count, silently clamped to actual available candidates
     * @param return_index Output vector populated with the best matching memory indices
     * @param similarities Output vector populated with the corresponding descending similarity scores
     * @param selected_indexes Optional candidate pool pre-filtered by IVF or metadata operations
     * @note Relies on highly optimised inner_product mathematics for distance calculations
     */
    void return_k_most_similar(const Vector &, size_t &, std::vector<std::size_t> &, std::vector<float> &, std::vector<size_t> * = nullptr);
};
#endif