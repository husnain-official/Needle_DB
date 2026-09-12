/**
 * @file vector_store.h
 * @brief Core in-memory storage engine managing flat vector arrays, metadata mappings, and similarity search operations.
 */
#pragma once
#include <vector>
#include <string>
#include <cstring>
#include <map>
#include <iostream>
#include <span>
#include <cmath>            // for sqrt
#include <algorithm>        // for std::sort and std::partial sort
#include <numeric>          // for std::inner_product (highly optimised dot-product)
#include <stdexcept>        // for thorw-catch blocks in parsing logic
#include <cstdint>          // for uint64_t
#include "types.h"          // for convinient structs
#include "similarities.hpp" // for similarity functions
#include "schema.hpp"
// #include "ivf.h"            // for ivf data

// --Index-Creation
class Vector_index;

// --Vector-store class
/**
 * @brief Primary memory-resident storage manager for vector embeddings, string identifiers, and associative metadata.
 */
class Vector_store
{
    std::vector<std::string> ids_;
    std::vector<float> embeddings_;
    std::map<std::string, std::map<std::string, std::string>> metadata_;
    std::vector<size_t> text_lengths_;
    std::vector<size_t> text_offsets_;
    std::size_t dims_ = schema::DIMENSIONS;
    std::size_t count_ = 0;
    Vector_index *index_ = nullptr;

public:
    /**
     * @brief Initializes an empty in-memory storage engine instance.
     */
    Vector_store() {}

    /**
     * @brief Destroys the active memory storage engine instance and releases internal array allocations.
     */
    ~Vector_store() {}
    // Getters
    /**
     * @brief Retrieves a raw pointer to a specific vector embedding within the flattened storage array.
     * @param i Zero-based internal memory index of the target vector.
     * @return Constant raw pointer to the contiguous block of floating-point data.
     */
    const float *get_embedding(const size_t i) const;

    /**
     * @brief Retrieves the unique string identifier associated with a specific memory index.
     * @param i Zero-based internal memory index of the target vector.
     * @return Constant reference to the string identifier.
     */
    const std::string &get_id(const size_t i) const;

    /**
     * @brief Retrieves the current count of active records residing in the memory storage engine.
     * @return Count of active vectors loaded in RAM.
     */
    const std::size_t get_count() const;

    /**
     * @brief Retrieves the byte length of the associated text payload for a specific memory index.
     * @param i Zero-based internal memory index of the target vector.
     * @return Byte length of the text payload.
     */
    const std::size_t get_text_length(const size_t i) const;

    /**
     * @brief Retrieves the absolute byte offset in the text database file associated with a specific memory index.
     * @param i Zero-based internal memory index of the target vector.
     * @return Absolute byte offset where the text payload begins.
     */
    const std::size_t get_text_offset(const size_t i) const;

    /**
     * @brief Locates the internal flat array offset of a specific string identifier.
     * @param id Target string identifier to search.
     * @return Zero-based internal memory index, or negative one if unfound.
     * @warning Executes an unoptimized linear O(N) scan across all stored IDs.
     */
    const int64_t get_index_in_ram(const std::string &) const;

    /**
     * @brief Filters stored vectors against required key-value metadata pairs.
     * @param mdata_arr Array containing structured metadata conditional constraints.
     * @param mdata_pairs_entered Number of active query constraints provided in the array.
     * @param matching_indices Output vector populated with memory indices satisfying all conditions.
     * @return Carrier structure containing operation success status and a diagnostic message on failure.
     * @note Empty query keys act as wildcards matching all active entries.
     */
    Parse_result get_matching_indices(const Metadata_entry *, const size_t &, std::vector<size_t> &);

    /**
     * @brief Retrieves a pointer to the attached nearest-neighbor indexing structure.
     * @return Pointer to the active vector index instance, or nullptr if unattached.
     */
    Vector_index *get_index() const;
    // Setters
    /**
     * @brief Validates and assigns the runtime embedding dimensionality against the engine schema.
     * @param dim Target dimensionality extracted from the persistent database file header.
     * @return Carrier structure containing operation success status and a diagnostic message on failure.
     */
    Parse_result set_dims_(const std::size_t);

    /**
     * @brief Overrides the internal tracker for live vector counts.
     * @param count Integer representing total valid vectors loaded into memory.
     * @return Carrier structure containing operation success status and a diagnostic message on failure.
     * @warning Does not resize or allocate underlying physical storage arrays.
     */
    Parse_result set_count_(const int);

    /**
     * @brief Maps parsed key-value metadata entries to a specific vector identifier in memory.
     * @param mdata_arr Array of structured metadata entries to store.
     * @param id Target vector identifier to associate with the structured data.
     * @note Silently ignores and drops entries containing empty string keys.
     */
    void set_metadata(const Metadata_entry *, const std::string &);

    // Core-Functions
    /**
     * @brief Purges all vectors, identifiers, and metadata mappings from active memory.
     * @warning Does not touch or modify the persistent database files on disk.
     */
    void clear();

    /**
     * @brief Appends a new vector record into the contiguous flat memory arrays.
     * @param v Target vector structure containing the identifier, embedding data, and metadata to insert.
     * @note Automatically propagates the insertion to the attached IVF index if populated.
     * @warning Assumes duplicate ID validation has already occurred prior to invocation.
     */
    void make_entry(const Vector &);

    /**
     * @brief Executes an O(1) swap-and-pop deletion of a vector from memory.
     * @param id Target string identifier of the vector to permanently remove.
     * @return True upon successful removal, false if the identifier is not found in memory.
     * @note Mutates internal array indexing, requiring strict IVF index synchronization.
     */
    bool remove_entry(const std::string &);

    /**
     * @brief Binds an external nearest-neighbor indexing structure to the memory storage engine.
     * @param idx Pointer to a fully constructed vector index instance.
     */
    void attach_index(Vector_index *);

    // Core-Functions
    /**
     * @brief Scales a targeted floating-point vector to unit length in place.
     * @param vec Target vector container requiring L2 normalization.
     * @return True upon success, false if processing a zero-magnitude null vector.
     * @warning Mutates the provided vector directly without allocating a copy.
     */
    bool normalise_vector(std::vector<float> &);
    /**
     * @brief Scales a raw, contiguous floating-point vector to unit length in place.
     * @param vec Raw pointer to the target vector array requiring L2 normalization.
     * @return True upon success, false if processing a zero-magnitude null vector.
     * @note Assumes the underlying vector length identically matches the configured schema dimensionality.
     * @warning Mutates the provided memory block directly without allocating a copy.
     */
    bool normalise_vector(float *);
    /**
     * @brief Resolves raw integer memory indices back into their string identifiers.
     * @param read_ids Output vector populated with the mapped string identifiers.
     * @param index Input vector of memory offsets generated by a search operation.
     * @param top_k Maximum identifier count to extract, silently clamped to available indices.
     * @return True upon success, false if any requested index exceeds array bounds.
     */
    bool read_all_ids(std::vector<std::string> &, const std::vector<std::size_t> &, std::size_t &);
    /**
     * @brief Verifies the presence of a specific identifier within the active memory map.
     * @param id_to_check Target string identifier for validation.
     * @return True if located in memory, false otherwise.
     * @warning Executes an unoptimized linear O(N) scan across the identifier array.
     */
    bool id_exists(const std::string &) const;
    /**
     * @brief Verifies the presence of a specific C-string identifier within the active memory map.
     * @param id_to_check Target null-terminated string identifier for validation.
     * @return True if located in memory, false otherwise.
     * @warning Executes an unoptimized linear O(N) scan across the identifier array.
     */
    bool id_exists(const char *) const;

    //  Search-functions
    /**
     * @brief Performs an exhaustive dot-product similarity sweep across all stored vectors.
     * @param query Normalized floating-point target embedding.
     * @param top_n Number of highest scoring matches to retrieve.
     * @return Ranked list of identifiers paired with their computed descending similarity scores.
     * @note Bypasses the clustered IVF index completely.
     */
    std::vector<std::pair<std::string, float>> brute_force_search(const std::vector<float> &, const int);
    /**
     * @brief Calculates and partial-sorts the highest similarity matches for a query.
     * @param query_v Complete query record including requested metadata constraints and the target embedding.
     * @param top_k Target result count, silently clamped to actual available candidates.
     * @param return_index Output vector populated with the best matching memory indices.
     * @param similarities Output vector populated with the corresponding descending similarity scores.
     * @param selected_indexes Optional candidate pool pre-filtered by IVF or metadata operations.
     * @note Relies on highly optimized inner_product mathematics for distance calculations.
     */
    void return_k_most_similar(const Vector &, size_t &, std::vector<std::size_t> &, std::vector<float> &, std::vector<size_t> * = nullptr);
};
