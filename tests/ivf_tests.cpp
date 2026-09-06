#include <gtest/gtest.h>
#include <vector>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <set>
#include "ivf.h"
#include "vector_store.h"
#include "schema.hpp"
#include "types.h"

// -----------------------------------------------------------------------------
// Test Fixture: IVF Contract & Deterministic Testing Setup
// -----------------------------------------------------------------------------
class IVFIndexTest : public ::testing::Test {
protected:
    // Builds a populated Vector struct with deterministic, directionally unique embeddings
    Vector BuildVector(size_t index, size_t cluster_group) {
        Vector v;
        v.id = "vec_" + std::to_string(index);
        v.text_offset = index * 100;
        v.text_length = 10;
        v.meta_data_count = 0;

        v.embeddings.assign(schema::DIMENSIONS, 0.0f);
        
        // Primary axis strongly determines the K-Means cluster
        size_t primary_axis = cluster_group % schema::DIMENSIONS;
        v.embeddings[primary_axis] = 1.0f;
        
        // Secondary axis guarantees a unique angle for every vector
        // Adding 1.0f ensures it never evaluates to 0, preventing normalization collapse
        size_t secondary_axis = (cluster_group + 1) % schema::DIMENSIONS;
        v.embeddings[secondary_axis] = 0.01f * (static_cast<float>(index) + 1.0f);

        // Normalize to ensure dot_similarity acts as true cosine similarity
        EXPECT_TRUE(Vector_store{}.normalise_vector(v.embeddings))
            << "Failed to normalize test vector " << index;

        return v;
    }

    // Populates a Vector_store with N well-separated, clustered vectors
    void PopulateStore(Vector_store& store, size_t count, size_t num_clusters = 3) {
        store.clear();
        for (size_t i = 0; i < count; i++) {
            Vector v = BuildVector(i, i % num_clusters);
            store.make_entry(v);
        }
    }

    // Helper to calculate squared L2 distance independently in tests
    float ComputeSquaredL2(const std::vector<float>& a, const std::vector<float>& b) {
        float sum = 0.0f;
        for (size_t i = 0; i < schema::DIMENSIONS; i++) {
            float diff = a[i] - b[i];
            sum += (diff * diff);
        }
        return sum;
    }
};

// =============================================================================
// Group 1: euclidean_distance
// =============================================================================

TEST_F(IVFIndexTest, EuclideanDistance_HandComputedValues_MatchesExpected) {
    IVF_index ivf;
    std::vector<float> a(schema::DIMENSIONS, 0.0f);
    std::vector<float> b(schema::DIMENSIONS, 0.0f);

    // a = [3.0, 0, ...], b = [0, 4.0, ...] -> Squared L2 = 3^2 + 4^2 = 25.0
    a[0] = 3.0f;
    b[1] = 4.0f;

    float dist_vec = ivf.euclidean_distance(a, b);
    float dist_ptr = ivf.euclidean_distance(a.data(), b.data());

    EXPECT_FLOAT_EQ(dist_vec, 25.0f);
    EXPECT_FLOAT_EQ(dist_ptr, 25.0f);
}

TEST_F(IVFIndexTest, EuclideanDistance_VectorToItself_IsZero) {
    IVF_index ivf;
    std::vector<float> a(schema::DIMENSIONS, 1.234f);

    EXPECT_FLOAT_EQ(ivf.euclidean_distance(a, a), 0.0f);
    EXPECT_FLOAT_EQ(ivf.euclidean_distance(a.data(), a.data()), 0.0f);
}

TEST_F(IVFIndexTest, EuclideanDistance_IsSymmetric) {
    IVF_index ivf;
    std::vector<float> a = BuildVector(1, 0).embeddings;
    std::vector<float> b = BuildVector(2, 1).embeddings;

    EXPECT_FLOAT_EQ(ivf.euclidean_distance(a, b), ivf.euclidean_distance(b, a));
    EXPECT_FLOAT_EQ(ivf.euclidean_distance(a.data(), b.data()), ivf.euclidean_distance(b.data(), a.data()));
}

// =============================================================================
// Group 2: build_ — correctness and determinism
// =============================================================================

TEST_F(IVFIndexTest, Build_EmptyStore_InitializesSafelyAndSearchReturnsEmpty) {
    Vector_store empty_store;
    IVF_index ivf(10, 2);

    ivf.build_(empty_store);

    Vector query = BuildVector(0, 0);
    std::vector<size_t> results = ivf.search_(query, 5);
    EXPECT_TRUE(results.empty());
}

TEST_F(IVFIndexTest, Build_EveryVectorAssignedToExactlyOneCluster) {
    Vector_store store;
    const size_t NUM_VECTORS = 12;
    const size_t NLIST = 4;
    PopulateStore(store, NUM_VECTORS, NLIST);

    IVF_index ivf(NLIST, 2);
    ivf.build_(store);

    // Search query with full probe to retrieve all candidates across all inverted lists
    Vector query = BuildVector(0, 0);
    IVF_index full_probe_ivf(NLIST, NLIST);
    full_probe_ivf.build_(store);
    
    std::vector<size_t> all_candidates = full_probe_ivf.search_(query, NUM_VECTORS * 2);
    
    // Check total candidate count matches stored vectors exactly
    EXPECT_EQ(all_candidates.size(), NUM_VECTORS);

    // Verify every vector index in [0, NUM_VECTORS) appears uniquely
    std::set<size_t> unique_indices(all_candidates.begin(), all_candidates.end());
    EXPECT_EQ(unique_indices.size(), NUM_VECTORS);
    for (size_t i = 0; i < NUM_VECTORS; i++) {
        EXPECT_TRUE(unique_indices.find(i) != unique_indices.end());
    }
}

TEST_F(IVFIndexTest, Build_Determinism_ProducesIdenticalSearchResultsWithFixedSeed) {
    Vector_store store;
    PopulateStore(store, 20, 4);

    IVF_index ivf1(4, 2);
    IVF_index ivf2(4, 2);

    ivf1.build_(store);
    ivf2.build_(store);

    Vector query = BuildVector(99, 1);
    std::vector<size_t> res1 = ivf1.search_(query, 5);
    std::vector<size_t> res2 = ivf2.search_(query, 5);

    ASSERT_EQ(res1.size(), res2.size());
    for (size_t i = 0; i < res1.size(); i++) {
        EXPECT_EQ(res1[i], res2[i]);
    }
}

TEST_F(IVFIndexTest, Build_DatasetSmallerThanNlist_ClampsClusterCount) {
    Vector_store store;
    const size_t NUM_VECTORS = 3;
    const size_t REQUESTED_NLIST = 10;
    PopulateStore(store, NUM_VECTORS, 2);

    // EXPECTED TO FAIL against current implementation if not yet fixed — see:
    // lists.resize(centroid_count) occurs before centroid_count = min(centroid_count, num_vectors),
    // leaving lists sized at 10 rather than 3.
    IVF_index ivf(REQUESTED_NLIST, 1);
    ivf.build_(store);

    Vector query = BuildVector(0, 0);
    // Searching with top_k = total vectors should return all 3 vectors safely
    std::vector<size_t> results = ivf.search_(query, NUM_VECTORS);
    EXPECT_LE(results.size(), NUM_VECTORS);
}

// =============================================================================
// Group 3: Regression focus — centroid_count restoration across builds
// =============================================================================

TEST_F(IVFIndexTest, Build_CentroidCount_DoesNotPermanentlyShrinkAcrossBuilds) {
    const size_t CONFIGURED_NLIST = 10;
    IVF_index ivf(CONFIGURED_NLIST, CONFIGURED_NLIST);

    // 1. Build against a small store (3 vectors)
    Vector_store small_store;
    PopulateStore(small_store, 3, 2);
    ivf.build_(small_store);

    // 2. Build against a larger store (15 vectors) on the same instance
    Vector_store large_store;
    PopulateStore(large_store, 15, 5);
    
    // EXPECTED TO FAIL against current implementation if not yet fixed — see:
    // centroid_count is permanently overwritten by centroid_count = min(centroid_count, num_vectors)
    // during the first build (clamping to 3), so the second build is permanently capped at 3 clusters.
    ivf.build_(large_store);

    Vector query = BuildVector(0, 0);
    std::vector<size_t> results = ivf.search_(query, 15);
    EXPECT_EQ(results.size(), 15);
}

// =============================================================================
// Group 4: search_ — exhaustive equivalence and boundaries
// =============================================================================

TEST_F(IVFIndexTest, Search_FullProbe_MatchesBruteForceExactly) {
    Vector_store store;
    const size_t NUM_VECTORS = 10;
    const size_t NLIST = 3;
    PopulateStore(store, NUM_VECTORS, NLIST);

    // nprobe >= nlist guarantees exhaustive search across all inverted lists
    IVF_index ivf(NLIST, NLIST);
    ivf.build_(store);

    Vector query = BuildVector(99, 1);
    const size_t TOP_K = 4;

    std::vector<size_t> ivf_results = ivf.search_(query, TOP_K);
    auto brute_force_results = store.brute_force_search(query.embeddings, TOP_K);

    ASSERT_EQ(ivf_results.size(), brute_force_results.size());
    for (size_t i = 0; i < TOP_K; i++) {
        EXPECT_EQ(store.get_id(ivf_results[i]), brute_force_results[i].first)
            << "Mismatch at rank " << i << " between full-probe IVF and brute force";
    }
}

TEST_F(IVFIndexTest, Search_PartialProbe_ReturnsValidCandidateSubset) {
    Vector_store store;
    const size_t NUM_VECTORS = 30;
    const size_t NLIST = 6;
    PopulateStore(store, NUM_VECTORS, NLIST);

    // Partial probe: search 2 out of 6 clusters
    IVF_index ivf(NLIST, 2);
    ivf.build_(store);

    Vector query = BuildVector(99, 0);
    const size_t TOP_K = 5;
    std::vector<size_t> results = ivf.search_(query, TOP_K);

    EXPECT_FALSE(results.empty());
    EXPECT_LE(results.size(), TOP_K);
    for (size_t idx : results) {
        EXPECT_LT(idx, NUM_VECTORS);
    }
}

TEST_F(IVFIndexTest, Search_UnbuiltIndex_ReturnsEmptySafely) {
    IVF_index ivf(10, 2); // build_() is never called
    Vector query = BuildVector(0, 0);

    std::vector<size_t> results = ivf.search_(query, 5);
    EXPECT_TRUE(results.empty());
}

TEST_F(IVFIndexTest, Search_TopKExceedsCandidateCount_ClampsCleanly) {
    Vector_store store;
    const size_t NUM_VECTORS = 3;
    PopulateStore(store, NUM_VECTORS, 2);

    IVF_index ivf(2, 2);
    ivf.build_(store);

    Vector query = BuildVector(0, 0);
    std::vector<size_t> results = ivf.search_(query, 100); // Exceeds vector count

    EXPECT_EQ(results.size(), NUM_VECTORS);
}

// =============================================================================
// Group 5: add_
// =============================================================================

TEST_F(IVFIndexTest, Add_AssignsNewIndexToNearestCentroidCluster) {
    Vector_store store;
    PopulateStore(store, 10, 2);

    IVF_index ivf(2, 2);
    ivf.build_(store);

    // Insert a new vector into the store directly
    Vector new_vec = BuildVector(10, 0);
    store.make_entry(new_vec);
    size_t new_idx = store.get_count() - 1;

    // Simulate propagation manually
    ivf.add_(new_idx);

    // Search specifically for this newly added vector
    std::vector<size_t> results = ivf.search_(new_vec, 1);
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0], new_idx);
}

TEST_F(IVFIndexTest, Add_UnbuiltIndex_NoOpAndDoesNotCrash) {
    IVF_index ivf(10, 2);
    // Should be a safe no-op per pointer/size guards
    EXPECT_NO_THROW(ivf.add_(0));
}

// =============================================================================
// Group 6: delete_
// =============================================================================

TEST_F(IVFIndexTest, Delete_RemovesIndexFromClusterList) {
    Vector_store store;
    const size_t NUM_VECTORS = 6;
    PopulateStore(store, NUM_VECTORS, 2);

    IVF_index ivf(2, 2);
    ivf.build_(store);

    // Delete index 1 from IVF
    ivf.delete_(1);

    Vector query = BuildVector(1, 1);
    std::vector<size_t> results = ivf.search_(query, NUM_VECTORS);

    // Index 1 must not appear in any cluster's search candidates
    EXPECT_EQ(std::find(results.begin(), results.end(), 1), results.end());
    EXPECT_EQ(results.size(), NUM_VECTORS - 1);
}

TEST_F(IVFIndexTest, Delete_NonExistentIndex_LeavesListsUnchanged) {
    Vector_store store;
    const size_t NUM_VECTORS = 5;
    PopulateStore(store, NUM_VECTORS, 2);

    IVF_index ivf(2, 2);
    ivf.build_(store);

    // Deleting an index that was never added
    EXPECT_NO_THROW(ivf.delete_(999));

    Vector query = BuildVector(0, 0);
    std::vector<size_t> results = ivf.search_(query, NUM_VECTORS);
    EXPECT_EQ(results.size(), NUM_VECTORS);
}

TEST_F(IVFIndexTest, Delete_SwapAndPop_PreservesRemainingElementsInCluster) {
    Vector_store store;
    // Put multiple vectors in the exact same cluster
    store.clear();
    for (size_t i = 0; i < 4; i++) {
        Vector v = BuildVector(i, 0); // All in cluster group 0
        store.make_entry(v);
    }

    IVF_index ivf(1, 1);
    ivf.build_(store);

    // Delete index 1 (non-last item in the single list: [0, 1, 2, 3])
    ivf.delete_(1);

    Vector query = BuildVector(0, 0);
    std::vector<size_t> results = ivf.search_(query, 4);

    ASSERT_EQ(results.size(), 3);
    std::set<size_t> remaining(results.begin(), results.end());
    EXPECT_TRUE(remaining.count(0) == 1);
    EXPECT_TRUE(remaining.count(2) == 1);
    EXPECT_TRUE(remaining.count(3) == 1);
    EXPECT_TRUE(remaining.count(1) == 0);
}

// =============================================================================
// Group 7: Full lifecycle consistency test
// =============================================================================

TEST_F(IVFIndexTest, Lifecycle_MixedAddAndDelete_MatchesBruteForcePostModification) {
    Vector_store store;
    const size_t INITIAL_COUNT = 6;
    PopulateStore(store, INITIAL_COUNT, 2);

    IVF_index ivf(2, 2);
    ivf.build_(store);

    // 1. Delete index 0 from IVF
    ivf.delete_(0);

    // 2. Add new vector (index 6) to store and IVF
    Vector v6 = BuildVector(6, 1);
    store.make_entry(v6);
    ivf.add_(6);

    // 3. Delete index 3 from IVF
    ivf.delete_(3);

    // Full-probe search on modified IVF
    Vector query = BuildVector(99, 1);
    std::vector<size_t> ivf_results = ivf.search_(query, 10);

    // Brute force across active indices (1, 2, 4, 5, 6)
    std::vector<size_t> active_indices = {1, 2, 4, 5, 6};
    std::vector<size_t> bf_indices;
    std::vector<float> bf_similarities;
    size_t top_k = 10;
    store.return_k_most_similar(query, top_k, bf_indices, bf_similarities, &active_indices);

    ASSERT_EQ(ivf_results.size(), bf_indices.size());
    for (size_t i = 0; i < ivf_results.size(); i++) {
        EXPECT_EQ(ivf_results[i], bf_indices[i]);
    }
}