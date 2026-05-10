#include "ivf.h"

// Fixed Euclidean distance: Computes squared L2 distance (avoids costly sqrt)
// Overloaded to accept raw pointers for high-performance tight loops
float IVF_index::euclidean_distance(const float *v1, const float *v2, size_t dims) const
{
    float distance = 0;
    for (size_t i = 0; i < dims; i++)
    {
        float diff = v1[i] - v2[i];
        distance += (diff * diff); // Squaring the difference correctly
    }
    return distance;
}
float IVF_index::euclidean_distance(const std::vector<float> &v1, const std::vector<float> &v2) const
{
    return euclidean_distance(v1.data(), v2.data(), v1.size());
}

void IVF_index::build_(Vector_store &store)
{
    store_ref = &store;
    size_t num_vectors = store.get_count();
    size_t dims = store.get_dims();

    // Always initialize lists, even if empty
    lists.clear();
    lists.resize(centroid_count);
    centroids.clear();
    centroids.resize(centroid_count * dims, 0.0f);

    if (num_vectors == 0)
        return;

    // Cap centroid count to the total number of vectors if dataset is very small
    centroid_count = std::min(centroid_count, num_vectors);

    // --- Step 1: Initialize centroids randomly from existing data ---
    std::vector<size_t> indices(num_vectors);
    std::iota(indices.begin(), indices.end(), 0); // Fill with 0, 1, 2...

    std::mt19937 rng(42); // Fixed seed for reproducibility in debugging
    std::shuffle(indices.begin(), indices.end(), rng);

    for (size_t i = 0; i < centroid_count; i++)
    {
        const float *vec = store.get_embedding(indices[i]);
        std::copy(vec, vec + dims, centroids.begin() + (i * dims));
    }

    // --- Step 2: K-Means Clustering (fixed 10 iterations) ---
    for (int iter = 0; iter < 10; iter++)
    {
        // Clear lists for this iteration
        for (auto &list : lists)
            list.clear();

        // Assign all vectors to their nearest centroid
        for (size_t i = 0; i < num_vectors; i++)
        {
            const float *vec = store.get_embedding(i);
            size_t best_c = find_nearest_centroid(vec, dims);
            lists[best_c].push_back(i);
        }

        // Recalculate centroid positions based on assigned vectors
        for (size_t c = 0; c < centroid_count; c++)
        {
            if (lists[c].empty())
                continue; // Skip empty clusters

            std::vector<float> new_centroid(dims, 0.0f);
            for (size_t idx : lists[c])
            {
                const float *vec = store.get_embedding(idx);
                for (size_t d = 0; d < dims; d++)
                {
                    new_centroid[d] += vec[d];
                }
            }

            // Average the positions
            for (size_t d = 0; d < dims; d++)
            {
                centroids[c * dims + d] = new_centroid[d] / lists[c].size();
            }
        }
    }
}

std::vector<size_t> IVF_index::search_(const Vector &query, size_t top_k)
{
    if (!store_ref || lists.empty())
        return {};

    size_t dims = store_ref->get_dims();

    // 1. Find the top 'nprobe' closest centroids to the query
    std::vector<std::pair<float, size_t>> centroid_dists;
    centroid_dists.reserve(centroid_count);

    for (size_t i = 0; i < centroid_count; i++)
    {
        float dist = euclidean_distance(query.data.data(), centroids.data() + (i * dims), dims);
        centroid_dists.push_back({dist, i});
    }

    // Sort ascending by Euclidean distance
    std::sort(centroid_dists.begin(), centroid_dists.end());
    size_t actual_probes = std::min(nprobe, centroid_count);

    // 2. Gather candidate vectors from those nearest clusters
    std::vector<Query_result> candidates;
    for (size_t p = 0; p < actual_probes; p++)
    {
        size_t c_idx = centroid_dists[p].second;
        for (size_t v_idx : lists[c_idx])
        {
            const float *vec = store_ref->get_embedding(v_idx);
            // We use dot_similarity here to match standard VectorStore logic (assuming normalized vectors)
            float sim = dot_similarity(query.data, vec);
            candidates.push_back({sim, v_idx});
        }
    }

    // 3. Sort candidates by similarity descending
    std::sort(candidates.begin(), candidates.end(), [](const Query_result &a, const Query_result &b)
              { return a.similarity > b.similarity; });

    // 4. Return top-K IDs
    std::vector<size_t> results;
    size_t result_count = std::min(top_k, candidates.size());
    results.reserve(result_count);
    for (size_t i = 0; i < result_count; i++)
    {
        results.push_back(candidates[i].index);
    }

    return results;
}

void IVF_index::add_(std::size_t index)
{
    if (!store_ref || centroids.empty() || lists.empty())
        return;

    size_t dims = store_ref->get_dims();
    const float *vec = store_ref->get_embedding(index);
    size_t nearest = find_nearest_centroid(vec, dims);
    lists[nearest].push_back(index);
}

void IVF_index::delete_(std::size_t index)
{
    // Remove index from whichever list holds it
    for (auto &list : lists)
    {
        auto it = std::find(list.begin(), list.end(), index);
        if (it != list.end())
        {
            // Swap-and-pop O(1) deletion within the cluster list
            std::swap(*it, list.back());
            list.pop_back();
            return;
        }
    }
}

// Helper to find the closest centroid to a given vector
size_t IVF_index::find_nearest_centroid(const float *vec, size_t dims) const
{
    float best_dist = std::numeric_limits<float>::max();
    size_t best_idx = 0;

    for (size_t i = 0; i < centroid_count; i++)
    {
        float dist = euclidean_distance(vec, centroids.data() + (i * dims), dims);
        if (dist < best_dist)
        {
            best_dist = dist;
            best_idx = i;
        }
    }
    return best_idx;
}
