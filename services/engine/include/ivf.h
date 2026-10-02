/**
 * @file ivf.h
 * @brief Defines the abstract vector index interface and the concrete Inverted File Index (IVF) implementation for approximate nearest-neighbor similarity clustering.
 */
#pragma once
#include "vector_store.h"
#include "similarities.hpp"
#include <vector>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <algorithm>
#include "schema.hpp"
// --- Abstract Base Class ---
/**
 * @brief Abstract base class defining the standard interface for vector similarity search indexes.
 */
class Vector_index
{
public:
    /**
     * @brief Constructs the underlying index structure using currently stored vectors.
     * @param store Reference to the active memory storage engine containing raw embeddings.
     */
    virtual void build_(Vector_store &store) = 0;
    /**
     * @brief Executes an approximate nearest-neighbor query against the indexed topology.
     * @param query Fully populated target vector including requested dimensionality and constraints.
     * @param top_k Maximum number of closest matches to retrieve.
     * @return Vector of raw memory indices representing nearest candidate matches.
     */
    virtual std::vector<size_t> search_(const Vector &query, size_t top_k) = 0;
    /**
     * @brief Inserts a newly appended vector into the existing cluster topology.
     * @param index Zero-based internal memory index of the new vector within the storage array.
     */
    virtual void add_(std::size_t index) = 0;
    /**
     * @brief Removes a specific vector from the cluster adjacency lists.
     * @param index Zero-based internal memory index targeted for eviction.
     */
    virtual void delete_(std::size_t index) = 0;
    /**
     * @brief Virtual destructor ensuring proper memory cleanup of derived index implementations.
     */
    virtual ~Vector_index() = default;
};

// --- Inverted File Index (IVF) Implementation ---
/**
 * @brief Inverted File Index (IVF) implementation utilizing k-means clustering for accelerated approximate nearest-neighbor searches.
 */
class IVF_index : public Vector_index
{
private:
    size_t centroid_count = 0;
    size_t nprobe = schema::MAX_PROBES_SEARCH; // Number of clusters to search during querying

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
     * @brief Configures the Inverted File Index (IVF) k-means clustering parameters for approximate nearest-neighbor searching.
     * @param nlist Target number of centroids to generate during index construction, defaulting to the schema maximum.
     * @param nprobe Number of adjacent clusters to evaluate during search execution, defaulting to the schema configuration.
     * @note Centroid count automatically clamps to the total active vector count during index construction.
     */
    IVF_index(size_t nlist = schema::MAX_CENTROIDS, size_t nprobe = schema::MAX_PROBES_SEARCH) : centroid_count(nlist), nprobe(nprobe) {}
    /**
     * @brief Safely destroys the IVF index instance and cleans up internal cluster assignments.
     */
    ~IVF_index() override = default;

    // Overloaded to accept raw pointers for high-performance tight loops
    /**
     * @brief Computes the squared L2 distance between two contiguous memory blocks.
     * @param v1 Raw pointer to the first floating-point sequence.
     * @param v2 Raw pointer to the second floating-point sequence.
     * @return Unrooted scalar representing the squared Euclidean distance.
     * @note Relies on the configured schema dimensions to determine the evaluation length.
     * @warning Omits the final square root operation for optimization purposes.
     */
    float euclidean_distance(const float *v1, const float *v2) const;
    /**
     * @brief Computes the squared L2 distance between two standard vector containers.
     * @param v1 First floating-point vector sequence.
     * @param v2 Second floating-point vector sequence.
     * @return Unrooted scalar representing the squared Euclidean distance.
     * @note Caller must ensure identical sequence lengths to prevent memory access violations.
     * @warning Omits the final square root operation for optimization purposes.
     */
    float euclidean_distance(const std::vector<float> &v1, const std::vector<float> &v2) const;
    /**
     * @brief Executes k-means clustering to partition stored vectors into Inverted File Index (IVF) lists.
     * @param store Active memory storage engine holding the reference embeddings.
     * @note Destroys and rebuilds the entire centroid topology from scratch.
     * @warning Caches a raw reference to the store, requiring the store to outlive the index.
     */
    void build_(Vector_store &store) override;
    /**
     * @brief Scans the closest clustered lists to isolate top candidate memory indices.
     * @param query Fully populated target vector structure including the query embedding.
     * @param top_k Upper bound limit for returned candidate matches.
     * @return Vector of raw memory indices representing potential candidate matches.
     * @warning Fails silently and returns an empty set if the index remains unbuilt.
     */
    std::vector<size_t> search_(const Vector &query, size_t top_k) override;
    /**
     * @brief Assigns a newly appended vector to the closest k-means centroid cluster.
     * @param index Zero-based internal memory index of the new vector within the storage array.
     * @note Calculates the Euclidean distance to all centroids to determine the optimal cluster assignment.
     */
    void add_(std::size_t index) override;
    /**
     * @brief Removes a specific vector from its assigned centroid cluster list via an O(1) swap-and-pop eviction.
     * @param index Zero-based internal memory index targeted for eviction.
     * @warning Executes an unoptimized linear O(N) scan across the cluster lists to locate the target index before eviction.
     */
    void delete_(std::size_t index) override;
    // Helpers
    /**
     * @brief Binds an active memory storage engine reference to the Inverted File Index (IVF).
     * @param store Constant reference to the active storage engine containing the raw embeddings.
     * @warning The referenced storage engine must outlive the index instance to prevent dangling pointers.
     */
    void set_ref_store(const Vector_store &store);
    /**
     * @brief Populates the internal k-means topology utilizing a pre-computed sequence of centroids.
     * @param loaded_centroids Flattened floating-point vector of centroid coordinates, transferred via move semantics.
     * @note Typically invoked during initialization to restore the IVF index state from persistent disk storage.
     */
    void set_centroids(std::vector<float> &&loaded_centroids);
    /**
     * @brief Assigns all currently loaded vectors to their nearest k-means centroid to populate the inverted lists.
     * @note Iterates through the entire memory storage engine to fully construct the initial cluster topology.
     */
    void build_lists();
    // Getters
    /**
     * @brief Retrieves the total number of k-means centroids currently generated within the index.
     * @return Count of active centroids defining the cluster topology.
     */
    const size_t get_built_centroids_number_() const;
    /**
     * @brief Retrieves a raw pointer to the contiguous block of floating-point centroid coordinates.
     * @return Constant raw pointer to the flattened centroid data array.
     * @note Primarily utilized for synchronizing the index topology to persistent disk storage.
     */
    const float *get_centroids_data_ptr_() const;

private:
    // Helper to find the closest centroid to a given vector
    /**
     * @brief Scans all flattened centroid blocks to locate the geometric nearest centroid.
     * @param vec Raw pointer to the target query embedding sequence.
     * @param dims Dimensionality span of the incoming query vector.
     * @return Zero-based sequential index of the closest centroid cluster.
     * @note Assumes internal centroid arrays are fully allocated and populated.
     */
    size_t find_nearest_centroid(const float *vec, size_t dims) const;
};
