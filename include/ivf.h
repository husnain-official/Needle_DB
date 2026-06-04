#ifndef IVF_H
#define IVF_H
#include "vector_store.h"
#include "similarities.hpp"
#include <vector>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <algorithm>

// --- Abstract Base Class ---
class Vector_index
{
public:
    /**
     * @brief Constructs the underlying index structure using currently stored vectors.
     * @param store Reference to the active storage engine containing raw embeddings
     */
    virtual void build_(Vector_store &store) = 0;
    /**
     * @brief Executes an approximate nearest-neighbour query against the indexed topology.
     * @param query Fully populated target vector including requested dimensionality
     * @param top_k Maximum number of closest matches to retrieve
     * @return Collection of raw memory indices representing nearest candidates
     */
    virtual std::vector<size_t> search_(const Vector &query, size_t top_k) = 0;
    /**
     * @brief Inserts a newly appended vector index into the existing cluster topology.
     * @param index Raw memory offset of the new vector within the storage array
     */
    virtual void add_(std::size_t index) = 0;
    /**
     * @brief Removes a specific vector index from the cluster adjacency lists.
     * @param index Raw memory offset targeted for eviction
     */
    virtual void delete_(std::size_t index) = 0;
    virtual ~Vector_index() = default;
};

// --- Inverted File Index (IVF) Implementation ---
class IVF_index : public Vector_index
{
private:
    size_t centroid_count = 0;
    size_t nprobe = 5; // Number of clusters to search during querying

    // Flattened 1D array for centroids
    // Accessed via: centroids[centroid_index * dims + d]
    std::vector<float> centroids;

    // Adjacency lists: mapping a centroid index to a list of vector IDs in the DB
    std::vector<std::vector<std::size_t>> lists;

    // Pointer to the store to retrieve embeddings (required for computing distances on the fly)
    const Vector_store *store_ref = nullptr;

public:
    // Constructor allows setting target number of clusters (nlist) and probes (nprobe)
    /**
     * @brief Configures the k-means clustering parameters for approximate searching.
     * @param nlist Target number of centroids to generate during index construction
     * @param nprobe Number of adjacent clusters to evaluate during search execution
     * @note Centroid count automatically clamps to total vector count during build
     */
    IVF_index(size_t nlist = 100, size_t nprobe = 5) : centroid_count(nlist), nprobe(nprobe) {}
    ~IVF_index() override = default;

    // Fixed Euclidean distance: Computes squared L2 distance (avoids costly sqrt)
    // Overloaded to accept raw pointers for high-performance tight loops
    /**
     * @brief Computes the squared L2 distance between two contiguous memory blocks.
     * @param v1 Pointer to the first floating-point sequence
     * @param v2 Pointer to the second floating-point sequence
     * @param dims Total floating-point elements to evaluate
     * @return Unrooted scalar representing the squared Euclidean distance
     * @warning Omits the final square root operation for optimization purposes
     */
    float euclidean_distance(const float *v1, const float *v2, size_t dims) const;
    /**
     * @brief Computes the squared L2 distance between two standard vector objects.
     * @param v1 First floating-point sequence
     * @param v2 Second floating-point sequence
     * @return Unrooted scalar representing the squared Euclidean distance
     * @warning Omits the final square root operation for optimization purposes
     * @note Caller must ensure identical sequence lengths to prevent segmentation faults
     */
    float euclidean_distance(const std::vector<float> &v1, const std::vector<float> &v2) const;
    /**
     * @brief Executes k-means clustering to partition stored vectors into inverted lists.
     * @param store Active memory store holding the reference embeddings
     * @note Destroys and rebuilds the entire centroid topology from scratch
     * @warning Caches a raw pointer to the store, requiring the store to outlive the index
     */
    void build_(Vector_store &store) override;
    /**
     * @brief Scans the closest clustered lists to isolate top candidate indices.
     * @param query Formatted target vector structure
     * @param top_k Upper bound limit for returned candidate matches
     * @return Memory indices of potential matches sorted by descending similarity
     * @warning Fails silently returning an empty set if the index remains unbuilt
     */
    std::vector<size_t> search_(const Vector &query, size_t top_k) override;
    /**
     * @brief Assigns a memory offset to the geometrically nearest existing centroid.
     * @param index Target vector offset within the attached storage engine
     * @warning Skips assignment if the index structures remain uninitialized
     */
    void add_(std::size_t index) override;
    /**
     * @brief Executes an O(1) swap-and-pop eviction from the assigned cluster list.
     * @param index Target memory offset to purge
     * @warning Degrades to an O(N) linear scan across cluster lists to locate the target index
     */
    void delete_(std::size_t index) override;

private:
    // Helper to find the closest centroid to a given vector
    /**
     * @brief Scans all flattened centroid blocks to locate the geometric nearest neighbor.
     * @param vec Pointer to the raw query embedding block
     * @param dims Dimensionality span of the incoming query
     * @return Zero-indexed position of the closest centroid cluster
     * @note Assumes centroid arrays are fully allocated and populated
     */
    size_t find_nearest_centroid(const float *vec, size_t dims) const;
};

#endif
