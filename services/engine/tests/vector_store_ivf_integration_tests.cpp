#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <iostream>
#include <algorithm>
#include "vector_store.h"
#include "ivf.h"
#include "schema.hpp"
#include "types.h"

// =============================================================================
// Group A: Correctness & Specification Tests
// =============================================================================

class VectorStoreIVFIntegrationTest : public ::testing::Test {
protected:
    Vector_store store;
    // For correctness tests, nprobe == nlist guarantees exact-match equivalent to brute force.
    IVF_index index{3, 3}; 

    void SetUp() override {
        // This is the critical wiring this entire suite is meant to test
        store.attach_index(&index);
    }

    // Builds a populated Vector struct with deterministic embeddings, and normalizes it.
    Vector BuildNormalizedVector(size_t idx, size_t cluster_group) {
        Vector v;
        std::string id_str = "vec_" + std::to_string(idx);
        id_str.resize(schema::ID_LENGTH, '\0');
        v.id = id_str;
        
        v.text_offset = idx * 100;
        v.text_length = 10;
        v.meta_data_count = 0;

        v.embeddings.assign(schema::DIMENSIONS, 0.0f);
        size_t primary_axis = cluster_group % schema::DIMENSIONS;
        v.embeddings[primary_axis] = 1.0f + static_cast<float>(idx) * 0.01f;
        
        size_t secondary_axis = (cluster_group + 1) % schema::DIMENSIONS;
        v.embeddings[secondary_axis] = 0.5f * static_cast<float>(idx % 3);

        // FIX: when idx % 3 == 0, secondary_axis becomes exactly 0, so any two vectors
        // sharing the same cluster_group with idx % 3 == 0 (e.g. vec_0 and vec_3) have only
        // one nonzero component and collapse onto the IDENTICAL unit direction after
        // normalization, regardless of their differing primary-axis magnitude. This tertiary
        // axis is unique per index (idx + 1 is never zero) so every vector stays
        // geometrically distinct post-normalization, eliminating similarity ties.
        size_t tertiary_axis = (cluster_group + 2) % schema::DIMENSIONS;
        v.embeddings[tertiary_axis] = 0.001f * static_cast<float>(idx + 1);

        // Crucial invariant: all vectors must be normalized before insertion
        bool norm_success = store.normalise_vector(v.embeddings);
        EXPECT_TRUE(norm_success) << "Failed to normalize test vector " << idx;

        return v;
    }
};

TEST_F(VectorStoreIVFIntegrationTest, BasicPropagation_InsertAndSearch_Succeeds) {
    std::vector<Vector> vecs;
    for (size_t i = 0; i < 6; i++) {
        vecs.push_back(BuildNormalizedVector(i, i % 3));
        store.make_entry(vecs.back()); // Auto-propagates to IVF via add_
    }

    index.build_(store);

    Vector query = vecs[2];
    std::vector<size_t> results = index.search_(query, 1);
    
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(store.get_id(results[0]), query.id);
}

// Regression coverage: Middle element deletion swap-and-pop desync
TEST_F(VectorStoreIVFIntegrationTest, DeletePropagation_MiddleElement_MaintainsConsistency) {
    std::vector<Vector> vecs;
    for (size_t i = 0; i < 5; i++) {
        vecs.push_back(BuildNormalizedVector(i, i % 3));
        store.make_entry(vecs.back());
    }

    index.build_(store);

    std::string deleted_id = vecs[2].id;
    std::string swapped_id = vecs[4].id; // The last element, which will be swapped into index 2
    Vector swapped_query = vecs[4];
    Vector deleted_query = vecs[2];

    EXPECT_TRUE(store.remove_entry(deleted_id)); // Auto-propagates to IVF via delete_
    
    EXPECT_EQ(store.get_count(), 4);
    EXPECT_FALSE(store.id_exists(deleted_id));
    EXPECT_EQ(store.get_index_in_ram(deleted_id), -1);

    // Assert deleted vector is completely gone from index
    std::vector<size_t> deleted_results = index.search_(deleted_query, 4);
    for (size_t idx : deleted_results) {
        EXPECT_NE(store.get_id(idx), deleted_id) 
            << "Deleted ID was still found by IVF search!";
    }

    // Direct regression check: Ensure the swapped element is still tracked perfectly
    std::vector<size_t> swapped_results = index.search_(swapped_query, 1);
    ASSERT_EQ(swapped_results.size(), 1);
    EXPECT_EQ(store.get_id(swapped_results[0]), swapped_id) 
        << "EXPECTED TO FAIL if swap-and-pop desync bug exists: IVF lost track of the swapped element.";
}

TEST_F(VectorStoreIVFIntegrationTest, DeletePropagation_FirstElement_MaintainsConsistency) {
    std::vector<Vector> vecs;
    for (size_t i = 0; i < 5; i++) {
        vecs.push_back(BuildNormalizedVector(i, i % 3));
        store.make_entry(vecs.back());
    }

    index.build_(store);

    std::string deleted_id = vecs[0].id;
    std::string swapped_id = vecs[4].id; 
    
    EXPECT_TRUE(store.remove_entry(deleted_id));
    EXPECT_EQ(store.get_count(), 4);

    std::vector<size_t> deleted_results = index.search_(vecs[0], 4);
    for (size_t idx : deleted_results) {
        EXPECT_NE(store.get_id(idx), deleted_id);
    }

    std::vector<size_t> swapped_results = index.search_(vecs[4], 1);
    ASSERT_EQ(swapped_results.size(), 1);
    EXPECT_EQ(store.get_id(swapped_results[0]), swapped_id);
}

TEST_F(VectorStoreIVFIntegrationTest, DeletePropagation_LastElement_MaintainsConsistency) {
    std::vector<Vector> vecs;
    for (size_t i = 0; i < 5; i++) {
        vecs.push_back(BuildNormalizedVector(i, i % 3));
        store.make_entry(vecs.back());
    }

    index.build_(store);

    std::string deleted_id = vecs[4].id; 
    // No swap occurs, but we still verify safety
    
    EXPECT_TRUE(store.remove_entry(deleted_id));
    EXPECT_EQ(store.get_count(), 4);

    std::vector<size_t> deleted_results = index.search_(vecs[4], 4);
    for (size_t idx : deleted_results) {
        EXPECT_NE(store.get_id(idx), deleted_id);
    }

    // Verify a surviving element
    std::vector<size_t> surviving_results = index.search_(vecs[3], 1);
    ASSERT_EQ(surviving_results.size(), 1);
    EXPECT_EQ(store.get_id(surviving_results[0]), vecs[3].id);
}

TEST_F(VectorStoreIVFIntegrationTest, DeletePropagation_UntilEmpty_MaintainsConsistencyAtEachStep) {
    std::vector<Vector> vecs;
    for (size_t i = 0; i < 4; i++) {
        vecs.push_back(BuildNormalizedVector(i, 0)); // All same cluster for stress testing the list
        store.make_entry(vecs.back());
    }

    index.build_(store);

    for (size_t i = 0; i < 4; i++) {
        EXPECT_TRUE(store.remove_entry(vecs[i].id));
        
        // Search should never return a deleted ID
        Vector query = vecs[i];
        std::vector<size_t> results = index.search_(query, 4);
        for (size_t idx : results) {
            EXPECT_NE(store.get_id(idx), vecs[i].id);
        }
        
        // Remaining live elements must still be findable
        for (size_t j = i + 1; j < 4; j++) {
            std::vector<size_t> live_res = index.search_(vecs[j], 1);
            ASSERT_FALSE(live_res.empty());
            EXPECT_EQ(store.get_id(live_res[0]), vecs[j].id);
        }
    }
    EXPECT_EQ(store.get_count(), 0);
}

TEST_F(VectorStoreIVFIntegrationTest, InsertDeleteChurnConsistency_MatchesBruteForceExactly) {
    std::vector<Vector> vecs;
    for (size_t i = 0; i < 10; i++) {
        vecs.push_back(BuildNormalizedVector(i, i % 3));
        store.make_entry(vecs.back());
    }

    index.build_(store);

    // Churn: Delete a few
    store.remove_entry(vecs[2].id);
    store.remove_entry(vecs[5].id);
    store.remove_entry(vecs[8].id);

    // Churn: Insert a few more
    for (size_t i = 10; i < 13; i++) {
        vecs.push_back(BuildNormalizedVector(i, i % 3));
        store.make_entry(vecs.back()); // Auto-propagates
    }

    // Cross-check IVF vs Brute Force on the churned state
    Vector query = vecs[11];
    const size_t TOP_K = 5;
    
    // index{3,3} guarantees exhaustive search across all clusters, perfectly matching brute-force
    std::vector<size_t> ivf_results = index.search_(query, TOP_K);
    auto bf_results = store.brute_force_search(query.embeddings, TOP_K);

    ASSERT_EQ(ivf_results.size(), bf_results.size());
    for (size_t i = 0; i < TOP_K; i++) {
        EXPECT_EQ(store.get_id(ivf_results[i]), bf_results[i].first)
            << "IVF index drifted from Vector_store arrays during churn sequence at rank " << i;
    }
}

TEST_F(VectorStoreIVFIntegrationTest, RebuildAfterChurn_IdempotentToLiveState) {
    for (size_t i = 0; i < 8; i++) {
        store.make_entry(BuildNormalizedVector(i, i % 3));
    }
    index.build_(store);
    
    // Churn
    store.remove_entry(store.get_id(1));
    store.remove_entry(store.get_id(4));
    store.make_entry(BuildNormalizedVector(8, 0));

    Vector query = BuildNormalizedVector(99, 1);
    std::vector<size_t> pre_rebuild_res = index.search_(query, 4);

    // Rebuild index simulating a LOAD command
    index.build_(store);

    std::vector<size_t> post_rebuild_res = index.search_(query, 4);

    ASSERT_EQ(pre_rebuild_res.size(), post_rebuild_res.size());
    for (size_t i = 0; i < pre_rebuild_res.size(); i++) {
        EXPECT_EQ(store.get_id(pre_rebuild_res[i]), store.get_id(post_rebuild_res[i]));
    }
}

// =============================================================================
// Group B: Curiosity / Performance Tests (Informational, Non-Gating)
// =============================================================================

class IVFPerformanceCuriosity : public ::testing::Test {
protected:
    Vector_store store;
    // Realistic partial-probe setup: 100 clusters, probe nearest 5
    IVF_index index{100, 5}; 

    // Adjustable constant: Tune this based on local machine speed.
    // 50,000 vectors * 1024 floats is ~200MB of embeddings.
    const size_t NUM_VECTORS = 50000; 

    void SetUp() override {
        store.attach_index(&index);
    }

    Vector GenerateRandomNormalizedVector(std::mt19937& rng, size_t idx) {
        Vector v;
        v.id = "rand_" + std::to_string(idx);
        v.id.resize(schema::ID_LENGTH, '\0');
        v.text_offset = 0;
        v.text_length = 1;
        v.meta_data_count = 0;
        v.embeddings.resize(schema::DIMENSIONS);

        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        for (size_t i = 0; i < schema::DIMENSIONS; i++) {
            v.embeddings[i] = dist(rng);
        }
        
        store.normalise_vector(v.embeddings);
        return v;
    }
};

TEST_F(IVFPerformanceCuriosity, BuildTiming_Informational) {
    std::mt19937 rng(42);
    for (size_t i = 0; i < NUM_VECTORS; i++) {
        store.make_entry(GenerateRandomNormalizedVector(rng, i));
    }

    auto start = std::chrono::steady_clock::now();
    index.build_(store);
    auto end = std::chrono::steady_clock::now();
    
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "\n[Curiosity] N=" << NUM_VECTORS 
              << ", dims=" << schema::DIMENSIONS 
              << ", nlist=100, nprobe=5\n";
    std::cout << "[Curiosity] index.build_() took: " << duration_ms << " ms\n";

    // Sanity check only
    EXPECT_GT(duration_ms, 0); 
}

TEST_F(IVFPerformanceCuriosity, SingleQueryTiming_Comparison) {
    std::mt19937 rng(42);
    for (size_t i = 0; i < NUM_VECTORS; i++) {
        store.make_entry(GenerateRandomNormalizedVector(rng, i));
    }
    index.build_(store);

    Vector query = GenerateRandomNormalizedVector(rng, 99999);
    const size_t TOP_K = 10;

    // Time Brute Force
    auto start_bf = std::chrono::steady_clock::now();
    auto bf_results = store.brute_force_search(query.embeddings, TOP_K);
    auto end_bf = std::chrono::steady_clock::now();
    auto duration_bf = std::chrono::duration_cast<std::chrono::microseconds>(end_bf - start_bf).count();

    // Time IVF Search
    auto start_ivf = std::chrono::steady_clock::now();
    auto ivf_results = index.search_(query, TOP_K);
    auto end_ivf = std::chrono::steady_clock::now();
    auto duration_ivf = std::chrono::duration_cast<std::chrono::microseconds>(end_ivf - start_ivf).count();

    float speedup = static_cast<float>(duration_bf) / std::max<float>(static_cast<float>(duration_ivf), 1.0f);

    std::cout << "\n[Curiosity] brute_force_search: " << duration_bf / 1000.0 << " ms | "
              << "ivf search_: " << duration_ivf / 1000.0 << " ms | "
              << "speedup: ~" << speedup << "x\n";

    // Minimal sanity checks, no exact ranking asserts since nprobe (5) < nlist (100)
    EXPECT_FALSE(bf_results.empty());
    EXPECT_FALSE(ivf_results.empty());
    EXPECT_LE(ivf_results.size(), TOP_K);
}

TEST_F(IVFPerformanceCuriosity, RepeatedQueryThroughput) {
    std::mt19937 rng(42);
    for (size_t i = 0; i < NUM_VECTORS; i++) {
        store.make_entry(GenerateRandomNormalizedVector(rng, i));
    }
    index.build_(store);

    const size_t NUM_QUERIES = 100;
    std::vector<Vector> queries;
    for (size_t i = 0; i < NUM_QUERIES; i++) {
        queries.push_back(GenerateRandomNormalizedVector(rng, i + 90000));
    }

    const size_t TOP_K = 10;

    auto start_bf = std::chrono::steady_clock::now();
    for (const auto& q : queries) {
        volatile auto res = store.brute_force_search(q.embeddings, TOP_K);
    }
    auto end_bf = std::chrono::steady_clock::now();
    auto total_bf_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_bf - start_bf).count();

    auto start_ivf = std::chrono::steady_clock::now();
    for (const auto& q : queries) {
        volatile auto res = index.search_(q, TOP_K);
    }
    auto end_ivf = std::chrono::steady_clock::now();
    auto total_ivf_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_ivf - start_ivf).count();

    std::cout << "\n[Curiosity] 100 queries Total Brute Force time: " << total_bf_ms << " ms (" 
              << static_cast<float>(total_bf_ms) / NUM_QUERIES << " ms/query)\n";
    std::cout << "[Curiosity] 100 queries Total IVF time: " << total_ivf_ms << " ms (" 
              << static_cast<float>(total_ivf_ms) / NUM_QUERIES << " ms/query)\n";
    
    EXPECT_GE(total_bf_ms, 0);
    EXPECT_GE(total_ivf_ms, 0);
}