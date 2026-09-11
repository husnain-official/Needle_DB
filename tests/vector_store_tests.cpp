#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <cmath>
#include <numeric>
#include "vector_store.h"
#include "schema.hpp"
#include "types.h"

// -----------------------------------------------------------------------------
// Test Fixture: Vector Store Contract Verification
// -----------------------------------------------------------------------------
class VectorStoreTest : public ::testing::Test {
protected:
    // Builds a fully-populated Vector struct derived strictly from schema constants
    Vector BuildVector(size_t variation, bool use_exact_id_length = true) {
        Vector v;
        
        // ID Generation
        std::string id_str = "id_" + std::to_string(variation);
        if (use_exact_id_length) {
            id_str.resize(schema::ID_LENGTH, '\0');
        }
        v.id = id_str;
        
        // Text Info Generation
        v.text_offset = variation * 1000;
        v.text_length = (variation * 10) % schema::TEXT_MAX_LENGTH;
        if (v.text_length == 0) {
            v.text_length = 1; // Ensure non-zero
        }
        
        // Metadata Generation
        v.meta_data_count = schema::META_DATA_KP_PAIRS;
        for (size_t i = 0; i < schema::META_DATA_KP_PAIRS; i++) {
            std::string key = "key" + std::to_string(i) + "_var" + std::to_string(variation);
            std::string val = "val" + std::to_string(i) + "_var" + std::to_string(variation);
            std::strncpy(v.meta_data[i].key, key.c_str(), schema::META_DATA_LENGTH);
            std::strncpy(v.meta_data[i].value, val.c_str(), schema::META_DATA_LENGTH);
        }
        
        // Embeddings Generation
        for (size_t i = 0; i < schema::DIMENSIONS; i++) {
            // Predictable, non-zero floats
            v.embeddings[i] = static_cast<float>(variation + 1) * 0.1f + static_cast<float>(i) * 0.01f;
        }
        
        return v;
    }

    // Asserts that a live record's internal parallel arrays strictly match the expected Vector
    void AssertVectorConsistency(const Vector_store& store, const Vector& expected) {
        int64_t idx = store.get_index_in_ram(expected.id);
        ASSERT_GE(idx, 0) << "Vector ID not found in RAM: " << expected.id;
        
        // Check IDs
        EXPECT_EQ(store.get_id(idx), expected.id);
        
        // Check Text Offset and Length
        EXPECT_EQ(store.get_text_offset(idx), expected.text_offset);
        EXPECT_EQ(store.get_text_length(idx), expected.text_length);
        
        // Check Embeddings
        const float* stored_emb = store.get_embedding(idx);
        for (size_t i = 0; i < schema::DIMENSIONS; i++) {
            EXPECT_FLOAT_EQ(stored_emb[i], expected.embeddings[i]);
        }
    }
};

// =============================================================================
// Group 1: make_entry / remove_entry (Round trip & Swap-and-Pop Correctness)
// =============================================================================

TEST_F(VectorStoreTest, RemoveEntry_FirstElement_PreservesAllOtherRecordsConsistency) {
    Vector_store store;
    std::vector<Vector> vecs;
    for (size_t i = 0; i < 5; i++) {
        vecs.push_back(BuildVector(i));
        store.make_entry(vecs.back());
    }
    
    EXPECT_TRUE(store.remove_entry(vecs[0].id));
    EXPECT_EQ(store.get_count(), 4);
    EXPECT_FALSE(store.id_exists(vecs[0].id));
    EXPECT_EQ(store.get_index_in_ram(vecs[0].id), -1);
    
    // Verify remaining elements
    for (size_t i = 1; i < 5; i++) {
        AssertVectorConsistency(store, vecs[i]);
    }
}

TEST_F(VectorStoreTest, RemoveEntry_MiddleElement_PreservesAllOtherRecordsConsistency) {
    Vector_store store;
    std::vector<Vector> vecs;
    for (size_t i = 0; i < 5; i++) {
        vecs.push_back(BuildVector(i));
        store.make_entry(vecs.back());
    }
    
    // Removing index 2 (middle). Index 4 will be swapped into index 2.
    EXPECT_TRUE(store.remove_entry(vecs[2].id));
    EXPECT_EQ(store.get_count(), 4);
    EXPECT_FALSE(store.id_exists(vecs[2].id));
    
    AssertVectorConsistency(store, vecs[0]);
    AssertVectorConsistency(store, vecs[1]);
    AssertVectorConsistency(store, vecs[3]);
    AssertVectorConsistency(store, vecs[4]); // Previously last element, now swapped
}

TEST_F(VectorStoreTest, RemoveEntry_LastElement_PreservesAllOtherRecordsConsistency) {
    Vector_store store;
    std::vector<Vector> vecs;
    for (size_t i = 0; i < 5; i++) {
        vecs.push_back(BuildVector(i));
        store.make_entry(vecs.back());
    }
    
    EXPECT_TRUE(store.remove_entry(vecs[4].id));
    EXPECT_EQ(store.get_count(), 4);
    EXPECT_FALSE(store.id_exists(vecs[4].id));
    
    for (size_t i = 0; i < 4; i++) {
        AssertVectorConsistency(store, vecs[i]);
    }
}

TEST_F(VectorStoreTest, RemoveEntry_UntilEmpty_PreservesConsistencyAtEachStep) {
    Vector_store store;
    std::vector<Vector> vecs;
    for (size_t i = 0; i < 3; i++) {
        vecs.push_back(BuildVector(i));
        store.make_entry(vecs.back());
    }
    
    for (size_t i = 0; i < 3; i++) {
        EXPECT_TRUE(store.remove_entry(vecs[i].id));
        for (size_t j = i + 1; j < 3; j++) {
            AssertVectorConsistency(store, vecs[j]);
        }
    }
    EXPECT_EQ(store.get_count(), 0);
}

TEST_F(VectorStoreTest, RemoveEntry_FollowedByMakeEntry_DoesNotReadStaleData) {
    Vector_store store;
    Vector v0 = BuildVector(0);
    Vector v1 = BuildVector(1);
    
    store.make_entry(v0);
    store.remove_entry(v0.id);
    
    // Insert v1 into the slot previously held by v0
    store.make_entry(v1);
    EXPECT_EQ(store.get_count(), 1);
    AssertVectorConsistency(store, v1);
    EXPECT_FALSE(store.id_exists(v0.id));
}

TEST_F(VectorStoreTest, RemoveEntry_NonExistentId_ReturnsFalseAndLeavesCountUnchanged) {
    Vector_store store;
    store.make_entry(BuildVector(0));
    
    EXPECT_FALSE(store.remove_entry("non_existent_id"));
    EXPECT_EQ(store.get_count(), 1);
}

// =============================================================================
// Group 2: get_matching_indices
// =============================================================================

TEST_F(VectorStoreTest, GetMatchingIndices_ZeroPairs_ReturnsAllIndices) {
    Vector_store store;
    store.make_entry(BuildVector(0));
    store.make_entry(BuildVector(1));
    
    std::vector<size_t> indices;
    Metadata_entry query_meta[schema::META_DATA_KP_PAIRS]{};
    Parse_result res = store.get_matching_indices(query_meta, 0, indices);
    
    EXPECT_TRUE(res.success);
    EXPECT_EQ(indices.size(), 2);
    // Wildcard matches everything
    EXPECT_TRUE(std::find(indices.begin(), indices.end(), 0) != indices.end());
    EXPECT_TRUE(std::find(indices.begin(), indices.end(), 1) != indices.end());
}

TEST_F(VectorStoreTest, GetMatchingIndices_NoMetadataStored_DoesNotMatch) {
    Vector_store store;
    Vector v = BuildVector(0);
    v.meta_data_count = 0;
    std::memset(v.meta_data, 0, sizeof(v.meta_data)); // Zero out storage
    store.make_entry(v);
    
    std::vector<size_t> indices;
    Metadata_entry query_meta[schema::META_DATA_KP_PAIRS]{};
    std::strncpy(query_meta[0].key, "k1", schema::META_DATA_LENGTH);
    std::strncpy(query_meta[0].value, "v1", schema::META_DATA_LENGTH);
    
    Parse_result res = store.get_matching_indices(query_meta, 1, indices);
    
    EXPECT_TRUE(res.success);
    EXPECT_TRUE(indices.empty());
}

TEST_F(VectorStoreTest, GetMatchingIndices_AllPairsMatch_ReturnsIndex) {
    Vector_store store;
    Vector v = BuildVector(0);
    store.make_entry(v);
    
    std::vector<size_t> indices;
    Parse_result res = store.get_matching_indices(v.meta_data, schema::META_DATA_KP_PAIRS, indices);
    
    EXPECT_TRUE(res.success);
    EXPECT_EQ(indices.size(), 1);
    EXPECT_EQ(indices[0], store.get_index_in_ram(v.id));
}

TEST_F(VectorStoreTest, GetMatchingIndices_OnePairMismatches_ReturnsEmpty) {
    Vector_store store;
    Vector v = BuildVector(0);
    store.make_entry(v);
    
    std::vector<size_t> indices;
    Metadata_entry query_meta[schema::META_DATA_KP_PAIRS]{};
    // Copy exact metadata
    for (size_t i = 0; i < schema::META_DATA_KP_PAIRS; i++) {
        query_meta[i] = v.meta_data[i];
    }
    // Mutate the value of the first pair (violates AND semantics)
    std::strncpy(query_meta[0].value, "wrong_val", schema::META_DATA_LENGTH);
    
    Parse_result res = store.get_matching_indices(query_meta, schema::META_DATA_KP_PAIRS, indices);
    
    EXPECT_TRUE(res.success);
    EXPECT_TRUE(indices.empty());
}

TEST_F(VectorStoreTest, GetMatchingIndices_StoreHasExtraMetadata_MatchesIfQueriedPairsMatch) {
    Vector_store store;
    Vector v = BuildVector(0);
    store.make_entry(v);
    
    std::vector<size_t> indices;
    Metadata_entry query_meta[schema::META_DATA_KP_PAIRS]{};
    // Only query the FIRST pair (leave others empty)
    query_meta[0] = v.meta_data[0];
    
    Parse_result res = store.get_matching_indices(query_meta, 1, indices);
    
    EXPECT_TRUE(res.success);
    EXPECT_EQ(indices.size(), 1);
}

// =============================================================================
// Group 3: normalise_vector 
// =============================================================================

TEST_F(VectorStoreTest, NormaliseVector_VectorRef_ComputesL2MagnitudeToOne) {
    Vector_store store;
    std::vector<float> vec(schema::DIMENSIONS, 2.0f); 
    
    EXPECT_TRUE(store.normalise_vector(vec));
    
    // Verify L2 magnitude is 1.0
    double magnitude_sq = 0.0;
    for (size_t i = 0; i < schema::DIMENSIONS; i++) {
        magnitude_sq += static_cast<double>(vec[i] * vec[i]);
    }
    EXPECT_NEAR(std::sqrt(magnitude_sq), 1.0, 1e-6);
}

TEST_F(VectorStoreTest, NormaliseVector_FloatPtr_ComputesL2MagnitudeToOne) {
    Vector_store store;
    std::vector<float> vec(schema::DIMENSIONS, 3.0f);
    
    EXPECT_TRUE(store.normalise_vector(vec.data()));
    
    double magnitude_sq = 0.0;
    for (size_t i = 0; i < schema::DIMENSIONS; i++) {
        magnitude_sq += static_cast<double>(vec[i] * vec[i]);
    }
    EXPECT_NEAR(std::sqrt(magnitude_sq), 1.0, 1e-6);
}

TEST_F(VectorStoreTest, NormaliseVector_PreservesDirectionRatio) {
    Vector_store store;
    std::vector<float> vec(schema::DIMENSIONS, 0.0f);
    vec[0] = 4.0f;
    vec[1] = 2.0f; // Ratio vec[0]/vec[1] = 2.0
    
    EXPECT_TRUE(store.normalise_vector(vec));
    EXPECT_FLOAT_EQ(vec[0] / vec[1], 2.0f);
}

TEST_F(VectorStoreTest, NormaliseVector_ZeroVector_ReturnsFalseWithoutNaN) {
    Vector_store store;
    std::vector<float> vec(schema::DIMENSIONS, 0.0f);
    
    EXPECT_FALSE(store.normalise_vector(vec));
    
    // Must remain finite, no NaNs
    for (size_t i = 0; i < schema::DIMENSIONS; i++) {
        EXPECT_TRUE(std::isfinite(vec[i]));
    }
}

TEST_F(VectorStoreTest, NormaliseVector_NearZeroVector_ReturnsFalse) {
    Vector_store store;
    std::vector<float> vec(schema::DIMENSIONS, 1e-11f);
    
    // Magnitude of this vector = sqrt(schema::DIMENSIONS * 1e-22) ≈ 3.2e-10
    // 3.2e-10 < 1e-9 documented threshold, so it must return false.
    EXPECT_FALSE(store.normalise_vector(vec));
}

// =============================================================================
// Group 4: brute_force_search / return_k_most_similar
// =============================================================================

TEST_F(VectorStoreTest, Search_MatchesHandComputedDotProducts_DescendingOrder) {
    Vector_store store;
    
    Vector v0 = BuildVector(0);
    Vector v1 = BuildVector(1);
    
    // Make completely predictable orthogonal/parallel vectors
    std::fill(v0.embeddings.begin(), v0.embeddings.end(), 0.0f);
    v0.embeddings[0] = 1.0f; 
    
    std::fill(v1.embeddings.begin(), v1.embeddings.end(), 0.0f);
    v1.embeddings[1] = 1.0f;
    
    store.make_entry(v0);
    store.make_entry(v1);
    
    // Query aligned with v0
    std::vector<float> query(schema::DIMENSIONS, 0.0f);
    query[0] = 1.0f; 
    
    auto results = store.brute_force_search(query, 2);
    
    ASSERT_EQ(results.size(), 2);
    // Highest dot product first (v0 = 1.0)
    EXPECT_EQ(results[0].first, v0.id); 
    EXPECT_FLOAT_EQ(results[0].second, 1.0f);
    // Lowest dot product second (v1 = 0.0)
    EXPECT_EQ(results[1].first, v1.id);
    EXPECT_FLOAT_EQ(results[1].second, 0.0f);
}

TEST_F(VectorStoreTest, ReturnKMostSimilar_TopKExceedsCount_ClampsSilently) {
    Vector_store store;
    store.make_entry(BuildVector(0));
    
    Vector query = BuildVector(99);
    size_t top_k = 5; // Request 5, but store only has 1
    std::vector<size_t> indices;
    std::vector<float> similarities;
    
    store.return_k_most_similar(query, top_k, indices, similarities, nullptr);
    
    EXPECT_EQ(top_k, 1);
    EXPECT_EQ(indices.size(), 1);
    EXPECT_EQ(similarities.size(), 1);
}

TEST_F(VectorStoreTest, ReturnKMostSimilar_WithSelectedIndexes_RestrictsToSubset) {
    Vector_store store;
    for (size_t i = 0; i < 4; i++) {
        store.make_entry(BuildVector(i));
    }
    
    Vector query = BuildVector(99);
    size_t top_k = 4;
    std::vector<size_t> indices;
    std::vector<float> similarities;
    
    // Restrict search to indices 1 and 3 only
    std::vector<size_t> selected_indices = {1, 3};
    store.return_k_most_similar(query, top_k, indices, similarities, &selected_indices);
    
    EXPECT_EQ(top_k, 2); // Clamped to available subset candidates
    EXPECT_EQ(indices.size(), 2);
    
    for (size_t idx : indices) {
        EXPECT_TRUE(idx == 1 || idx == 3);
    }
}

TEST_F(VectorStoreTest, ReturnKMostSimilar_NullptrSelectedIndexes_SearchesAll) {
    Vector_store store;
    for (size_t i = 0; i < 3; i++) {
        store.make_entry(BuildVector(i));
    }
    
    Vector query = BuildVector(99);
    size_t top_k = 3;
    std::vector<size_t> indices;
    std::vector<float> similarities;
    
    store.return_k_most_similar(query, top_k, indices, similarities, nullptr);
    EXPECT_EQ(indices.size(), 3);
}

TEST_F(VectorStoreTest, ReturnKMostSimilar_TiedScores_IncludesBothInResults) {
    Vector_store store;
    Vector v0 = BuildVector(0);
    Vector v1 = BuildVector(1);
    v1.embeddings = v0.embeddings; // Exact same embeddings -> exact tie
    
    store.make_entry(v0);
    store.make_entry(v1);
    
    size_t top_k = 2;
    std::vector<size_t> indices;
    std::vector<float> similarities;
    
    store.return_k_most_similar(v0, top_k, indices, similarities, nullptr);
    
    ASSERT_EQ(indices.size(), 2);
    // Order within tie isn't strictly defined, but both must exist in the result set
    bool found_0 = (indices[0] == 0 || indices[1] == 0);
    bool found_1 = (indices[0] == 1 || indices[1] == 1);
    EXPECT_TRUE(found_0);
    EXPECT_TRUE(found_1);
}

// =============================================================================
// Group 5: id_exists
// =============================================================================

TEST_F(VectorStoreTest, IdExists_ExactMatch_ReturnsTrueForBothOverloads) {
    Vector_store store;
    Vector v = BuildVector(0, false); // Do not pad with nulls to test raw string exactly
    v.id = "my_exact_id"; // Length 11
    store.make_entry(v);
    
    std::string str_id = "my_exact_id";
    EXPECT_TRUE(store.id_exists(str_id));
    EXPECT_TRUE(store.id_exists("my_exact_id"));
}

TEST_F(VectorStoreTest, IdExists_NoMatch_ReturnsFalse) {
    Vector_store store;
    store.make_entry(BuildVector(0));
    
    std::string str_id = "non_existent";
    EXPECT_FALSE(store.id_exists(str_id));
    EXPECT_FALSE(store.id_exists("non_existent"));
}

TEST_F(VectorStoreTest, IdExists_CaseSensitive_DifferingCaseDoesNotMatch) {
    Vector_store store;
    Vector v = BuildVector(0, false);
    v.id = "CASE_id";
    store.make_entry(v);
    
    EXPECT_FALSE(store.id_exists(std::string("case_id")));
}

TEST_F(VectorStoreTest, IdExists_ConstCharShorterThanSchema_TruncatesAndMatches) {
    Vector_store store;
    Vector v = BuildVector(0, false);
    v.id = "short"; // Length 5
    store.make_entry(v);
    
    // The const char* overload uses strnlen up to schema::ID_LENGTH.
    // It should construct exactly "short" and match.
    EXPECT_TRUE(store.id_exists("short"));
}

// =============================================================================
// Group 6: read_all_ids
// =============================================================================

TEST_F(VectorStoreTest, ReadAllIds_ValidIndices_ReturnsIdsInOrder) {
    Vector_store store;
    store.make_entry(BuildVector(0));
    store.make_entry(BuildVector(1));
    store.make_entry(BuildVector(2));
    
    std::vector<size_t> indices = {2, 0}; // Out of order query
    std::vector<std::string> read_ids;
    size_t top_k = 2;
    
    EXPECT_TRUE(store.read_all_ids(read_ids, indices, top_k));
    ASSERT_EQ(read_ids.size(), 2);
    
    EXPECT_EQ(read_ids[0], store.get_id(2));
    EXPECT_EQ(read_ids[1], store.get_id(0));
}

TEST_F(VectorStoreTest, ReadAllIds_TopKExceedsIndicesCount_ClampsTopK) {
    Vector_store store;
    store.make_entry(BuildVector(0));
    
    std::vector<size_t> indices = {0};
    std::vector<std::string> read_ids;
    size_t top_k = 5; 
    
    EXPECT_TRUE(store.read_all_ids(read_ids, indices, top_k));
    EXPECT_EQ(top_k, 1); // Updated by reference
    EXPECT_EQ(read_ids.size(), 1);
}

TEST_F(VectorStoreTest, ReadAllIds_StaleIndex_ReturnsFalseAndPreservesPriorProcessedIds) {
    Vector_store store;
    store.make_entry(BuildVector(0));
    store.make_entry(BuildVector(1));
    
    // 999 is invalid/stale
    std::vector<size_t> indices = {0, 999, 1}; 
    std::vector<std::string> read_ids;
    size_t top_k = 3;
    
    EXPECT_FALSE(store.read_all_ids(read_ids, indices, top_k));
    
    // Contractually, entries processed before the failure should remain in read_ids
    ASSERT_EQ(read_ids.size(), 1);
    EXPECT_EQ(read_ids[0], store.get_id(0));
}

// =============================================================================
// Group 7: clear
// =============================================================================

TEST_F(VectorStoreTest, Clear_ResetsCountAndStartsFresh) {
    Vector_store store;
    store.make_entry(BuildVector(0));
    store.make_entry(BuildVector(1));
    
    store.clear();
    EXPECT_EQ(store.get_count(), 0);
    
    Vector new_vec = BuildVector(2);
    store.make_entry(new_vec);
    
    EXPECT_EQ(store.get_count(), 1);
    EXPECT_EQ(store.get_index_in_ram(new_vec.id), 0); // Freshly starts at index 0
    AssertVectorConsistency(store, new_vec);
}

// =============================================================================
// Group 8: set_dims_
// =============================================================================

TEST_F(VectorStoreTest, SetDims_SchemaDimensions_Succeeds) {
    Vector_store store;
    Parse_result res = store.set_dims_(schema::DIMENSIONS);
    EXPECT_TRUE(res.success);
}

TEST_F(VectorStoreTest, SetDims_WrongDimensions_FailsAndDoesNotMutateStore) {
    Vector_store store;
    
    // Attempt incorrect dimensionality
    Parse_result res = store.set_dims_(schema::DIMENSIONS + 1);
    EXPECT_FALSE(res.success);
    
    // Store should not be mutated. A subsequent valid insert must succeed
    // with the original dimensionality.
    Vector v = BuildVector(0);
    store.make_entry(v); 
    EXPECT_EQ(store.get_count(), 1);
    
    // Check that we can successfully normalize it, which relies on the internal 
    // dimensions matching schema::DIMENSIONS
    std::vector<float> emb(schema::DIMENSIONS, 1.0f);
    EXPECT_TRUE(store.normalise_vector(emb));
}