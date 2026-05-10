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
    virtual void build_(const Vector_store &store) = 0;
    virtual std::vector<size_t> search_(const Vector &query, size_t top_k) = 0;
    virtual void add_(std::size_t index) = 0;
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
    IVF_index(size_t nlist = 100, size_t nprobe = 5) : centroid_count(nlist), nprobe(nprobe) {}
    ~IVF_index() override = default;

    // Fixed Euclidean distance: Computes squared L2 distance (avoids costly sqrt)
    // Overloaded to accept raw pointers for high-performance tight loops
    float euclidean_distance(const float *v1, const float *v2, size_t dims) const;
    float euclidean_distance(const std::vector<float> &v1, const std::vector<float> &v2) const;
    void build_(const Vector_store &store) override;
    std::vector<size_t> search_(const Vector &query, size_t top_k) override;
    void delete_(std::size_t index) override;

private:
    // Helper to find the closest centroid to a given vector
    size_t find_nearest_centroid(const float *vec, size_t dims) const;
    
};

#endif
