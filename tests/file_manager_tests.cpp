#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cstring>
#include <vector>
#include "file_manager.h"
#include "schema.hpp"

// -----------------------------------------------------------------------------
// Test Fixture: Environment Management & Data Builders
// -----------------------------------------------------------------------------
class FileManagerTest : public ::testing::Test
{
protected:
    std::string entry_path_;
    std::string text_path_;
    std::string index_path_;
    const std::string temp_compact_entry_ = "./data/temp_database.vdb";
    const std::string temp_compact_text_ = "./data/temp_text_database.vdb";

    void SetUp() override
    {
        // Ensure the hardcoded data directory used by compact() exists
        std::error_code ec;
        std::filesystem::create_directories("./data", ec);

        const ::testing::TestInfo *test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string test_name = test_info->name();

        // Generate unique paths per test to prevent cross-test contamination
        entry_path_ = "./temp_" + test_name + "_entry.vdb";
        text_path_ = "./temp_" + test_name + "_text.vdb";
        index_path_ = "./temp_" + test_name + "_index.vdb";

        CleanUpFiles();
    }

    void TearDown() override
    {
        CleanUpFiles();
    }

    void CleanUpFiles()
    {
        std::error_code ec;
        // Unconditional cleanup, ignoring "not found" errors
        std::filesystem::remove(entry_path_, ec);
        std::filesystem::remove(text_path_, ec);
        std::filesystem::remove(index_path_, ec);
        std::filesystem::remove(temp_compact_entry_, ec);
        std::filesystem::remove(temp_compact_text_, ec);
    }

    // Helper: Populates a DB_entry and its corresponding text blob strictly governed by schema limits
    void CreateValidEntry(DB_entry &entry, std::string &text, size_t variation = 0)
    {
        std::memset(&entry, 0, sizeof(DB_entry));
        entry.flag = 1;

        // Build exact-sized ID
        std::string id_str = "id_" + std::to_string(variation);
        id_str.resize(schema::ID_LENGTH, '\0');
        std::memcpy(entry.id, id_str.data(), schema::ID_LENGTH);

        // Build exact max allowed metadata pairs
        entry.meta_data_count = schema::META_DATA_KP_PAIRS;
        for (size_t i = 0; i < schema::META_DATA_KP_PAIRS; i++)
        {
            std::string key = "key_" + std::to_string(i);
            std::string val = "val_" + std::to_string(i);
            std::strncpy(entry.meta_data[i].key, key.c_str(), schema::META_DATA_LENGTH);
            std::strncpy(entry.meta_data[i].value, val.c_str(), schema::META_DATA_LENGTH);
        }

        // Build embedding floats up to exact schema limit
        for (size_t i = 0; i < schema::DIMENSIONS; i++)
        {
            entry.embeddings[i] = i * 0.1f + static_cast<float>(variation);
        }

        // Build text blob
        text = "Valid text payload variation " + std::to_string(variation);
        entry.text_length = text.size();
    }

    // Helper: Generates a distinct vector of floats for index/centroid testing
    std::vector<float> CreateCentroids(size_t centroid_count)
    {
        size_t total_floats = centroid_count * schema::DIMENSIONS;
        std::vector<float> centroids(total_floats);
        for (size_t i = 0; i < total_floats; i++)
        {
            centroids[i] = static_cast<float>(i) * 0.5f;
        }
        return centroids;
    }

    // Helper: Mutates the on-disk header for corruption tests
    void CorruptHeader(std::function<void(DB_header &)> modifier)
    {
        DB_header h;
        {
            std::fstream f(entry_path_, std::ios::in | std::ios::binary);
            ASSERT_TRUE(f.is_open());
            f.read(reinterpret_cast<char *>(&h), sizeof(DB_header));
        }
        modifier(h);
        {
            std::fstream f(entry_path_, std::ios::in | std::ios::out | std::ios::binary);
            ASSERT_TRUE(f.is_open());
            f.seekp(0);
            f.write(reinterpret_cast<const char *>(&h), sizeof(DB_header));
            f.flush();
        }
    }
};

// =============================================================================
// Group 1: Constructor — fresh database creation
// =============================================================================

TEST_F(FileManagerTest, Constructor_FreshCreation_InitializesEmptyDBAndIndex)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    EXPECT_EQ(fm.get_total_vector_count(), 0);
    EXPECT_EQ(fm.get_live_vector_count(), 0);
    EXPECT_FALSE(fm.is_index_populated());
    EXPECT_EQ(fm.get_index_size(), 0);

    // Verify file existence
    EXPECT_TRUE(std::filesystem::exists(entry_path_));
    EXPECT_TRUE(std::filesystem::exists(text_path_));
    EXPECT_TRUE(std::filesystem::exists(index_path_));
}

TEST_F(FileManagerTest, Constructor_HeaderMatchesSchemaPrecisely)
{
    {
        File_manager fm(entry_path_, text_path_, index_path_); // Triggers creation
    }

    File_manager fm_reader(entry_path_, text_path_, index_path_);
    DB_header h = fm_reader.read_header();

    EXPECT_EQ(std::strncmp(h.magic_number, schema::MAGIC_NUMBER, sizeof(schema::MAGIC_NUMBER)), 0);
    EXPECT_EQ(h.version, schema::VERSION);
    EXPECT_EQ(h.dimensions, schema::DIMENSIONS);
    EXPECT_EQ(h.id_length, schema::ID_LENGTH);
    EXPECT_EQ(h.kv_length, schema::META_DATA_LENGTH);
    EXPECT_EQ(h.max_kv, schema::META_DATA_KP_PAIRS);
}

// =============================================================================
// Group 2: Constructor — opening an existing, valid database (with index)
// =============================================================================

TEST_F(FileManagerTest, Constructor_OpenExisting_PreservesDataAndIndex)
{
    DB_entry original_entry{};
    std::string original_text;
    CreateValidEntry(original_entry, original_text);

    size_t test_centroid_count = 2;
    std::vector<float> original_centroids = CreateCentroids(test_centroid_count);

    {
        File_manager fm(entry_path_, text_path_, index_path_);
        EXPECT_TRUE(fm.write_entry(original_entry, original_text));
        EXPECT_TRUE(fm.write_index_(original_centroids.data(), test_centroid_count));
        EXPECT_EQ(fm.get_total_vector_count(), 1);
    } // fm destructs, closing files

    // Reopen
    File_manager fm_reopened(entry_path_, text_path_, index_path_);
    EXPECT_EQ(fm_reopened.get_total_vector_count(), 1);
    EXPECT_EQ(fm_reopened.get_live_vector_count(), 1);

    DB_entry read_back_entry{};
    std::string read_back_text;
    EXPECT_TRUE(fm_reopened.read_entry(0, read_back_entry, read_back_text));
    EXPECT_EQ(read_back_text, original_text);

    // Verify index persistence
    EXPECT_TRUE(fm_reopened.is_index_populated());
    std::vector<float> read_back_centroids = fm_reopened.read_index_(test_centroid_count);
    ASSERT_EQ(read_back_centroids.size(), original_centroids.size());
    for (size_t i = 0; i < original_centroids.size(); i++)
    {
        EXPECT_FLOAT_EQ(read_back_centroids[i], original_centroids[i]);
    }
}

// =============================================================================
// Group 3: Constructor — corrupted/mismatched schema rejection
// =============================================================================

TEST_F(FileManagerTest, Constructor_WrongMagicNumber_Throws)
{
    {
        File_manager fm(entry_path_, text_path_, index_path_);
    } // Create
    CorruptHeader([](DB_header &h)
                  { std::memcpy(h.magic_number, "BAD", 4); });
    EXPECT_THROW(File_manager(entry_path_, text_path_, index_path_), std::runtime_error);
}

TEST_F(FileManagerTest, Constructor_WrongVersion_Throws)
{
    {
        File_manager fm(entry_path_, text_path_, index_path_);
    }
    CorruptHeader([](DB_header &h)
                  { h.version = schema::VERSION + 1; });
    EXPECT_THROW(File_manager(entry_path_, text_path_, index_path_), std::runtime_error);
}

TEST_F(FileManagerTest, Constructor_WrongDimensions_Throws)
{
    {
        File_manager fm(entry_path_, text_path_, index_path_);
    }
    CorruptHeader([](DB_header &h)
                  { h.dimensions = schema::DIMENSIONS + 1; });
    EXPECT_THROW(File_manager(entry_path_, text_path_, index_path_), std::runtime_error);
}

TEST_F(FileManagerTest, Constructor_WrongIdLength_Throws)
{
    {
        File_manager fm(entry_path_, text_path_, index_path_);
    }
    CorruptHeader([](DB_header &h)
                  { h.id_length = schema::ID_LENGTH - 1; });
    EXPECT_THROW(File_manager(entry_path_, text_path_, index_path_), std::runtime_error);
}

TEST_F(FileManagerTest, Constructor_WrongKvLength_Throws)
{
    {
        File_manager fm(entry_path_, text_path_, index_path_);
    }
    CorruptHeader([](DB_header &h)
                  { h.kv_length = schema::META_DATA_LENGTH - 1; });
    EXPECT_THROW(File_manager(entry_path_, text_path_, index_path_), std::runtime_error);
}

TEST_F(FileManagerTest, Constructor_WrongMaxKv_Throws)
{
    {
        File_manager fm(entry_path_, text_path_, index_path_);
    }
    CorruptHeader([](DB_header &h)
                  { h.max_kv = schema::META_DATA_KP_PAIRS + 1; });
    EXPECT_THROW(File_manager(entry_path_, text_path_, index_path_), std::runtime_error);
}

// =============================================================================
// Group 4: Constructor — entry/text existence mismatch throws
// =============================================================================

TEST_F(FileManagerTest, Constructor_MissingTextFile_ThrowsAndPreservesEntryFile)
{
    {
        File_manager fm(entry_path_, text_path_, index_path_);
        DB_entry e{};
        std::string t;
        CreateValidEntry(e, t);
        fm.write_entry(e, t);
    }

    auto original_size = std::filesystem::file_size(entry_path_);
    std::filesystem::remove(text_path_);

    // Mismatch MUST throw, avoiding the old silent-recreation behavior
    EXPECT_THROW({ File_manager fm_reopened(entry_path_, text_path_, index_path_); }, std::runtime_error);

    // Surviving file must NOT be truncated
    EXPECT_EQ(std::filesystem::file_size(entry_path_), original_size);
}

TEST_F(FileManagerTest, Constructor_MissingEntryFile_ThrowsAndPreservesTextFile)
{
    {
        File_manager fm(entry_path_, text_path_, index_path_);
        DB_entry e{};
        std::string t;
        CreateValidEntry(e, t);
        fm.write_entry(e, t);
    }

    auto original_size = std::filesystem::file_size(text_path_);
    std::filesystem::remove(entry_path_);

    EXPECT_THROW({ File_manager fm_reopened(entry_path_, text_path_, index_path_); }, std::runtime_error);
    EXPECT_EQ(std::filesystem::file_size(text_path_), original_size);
}

TEST_F(FileManagerTest, Constructor_BothMissing_DoesNotThrow)
{
    // Both missing is the normal "fresh database" path
    EXPECT_NO_THROW({ File_manager fm(entry_path_, text_path_, index_path_); });
}

// =============================================================================
// Group 5: Constructor — index file is independent
// =============================================================================

TEST_F(FileManagerTest, Constructor_MissingIndexFile_CreatesEmptyWithoutThrow)
{
    {
        File_manager fm(entry_path_, text_path_, index_path_);
    }

    std::filesystem::remove(index_path_);

    // Reopening with valid entry/text but missing index must succeed and recreate the index cleanly
    File_manager fm_reopened(entry_path_, text_path_, index_path_);
    EXPECT_FALSE(fm_reopened.is_index_populated());
}

TEST_F(FileManagerTest, Constructor_EmptyIndexFile_Succeeds)
{
    {
        File_manager fm(entry_path_, text_path_, index_path_);
    }
    // index_path_ exists but is empty
    File_manager fm_reopened(entry_path_, text_path_, index_path_);
    EXPECT_FALSE(fm_reopened.is_index_populated());
}

TEST_F(FileManagerTest, Constructor_PopulatedIndexFile_SucceedsAndPreserves)
{
    size_t centroid_count = 3;
    std::vector<float> centroids = CreateCentroids(centroid_count);
    {
        File_manager fm(entry_path_, text_path_, index_path_);
        fm.write_index_(centroids.data(), centroid_count);
    }

    File_manager fm_reopened(entry_path_, text_path_, index_path_);
    EXPECT_TRUE(fm_reopened.is_index_populated());

    std::vector<float> read_back = fm_reopened.read_index_(centroid_count);
    EXPECT_EQ(read_back.size(), centroids.size());
}

// GAP: a fresh/empty entry+text pair can end up paired with a stale, non-empty index file left over
// from an unrelated prior dataset, since index-file freshness is not tied to entry/text
// freshness. Not asserted as a bug here, but flagged for a decision — should a fresh
// entry/text recreation also force-clear any pre-existing index file?
TEST_F(FileManagerTest, Constructor_MissingEntryTextButPopulatedIndex_Characterization)
{
    size_t centroid_count = 2;
    std::vector<float> centroids = CreateCentroids(centroid_count);
    {
        // Setup initial state and write an index
        File_manager fm(entry_path_, text_path_, index_path_);
        fm.write_index_(centroids.data(), centroid_count);
    }

    // Simulate entry/text loss
    std::filesystem::remove(entry_path_);
    std::filesystem::remove(text_path_);

    // Open again. Entry and Text will be recreated (empty), but Index will be stale.
    File_manager fm_reopened(entry_path_, text_path_, index_path_);

    EXPECT_EQ(fm_reopened.get_total_vector_count(), 0);
    EXPECT_TRUE(fm_reopened.is_index_populated()); // Leftover index data remains

    std::vector<float> read_back = fm_reopened.read_index_(centroid_count);
    ASSERT_EQ(read_back.size(), centroid_count * schema::DIMENSIONS);
    EXPECT_FLOAT_EQ(read_back[0], centroids[0]);
}

// =============================================================================
// Group 6: write_entry / read_entry — round trip
// =============================================================================

TEST_F(FileManagerTest, WriteRead_RoundTrip_ExactMatches)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    DB_entry write_e{};
    std::string write_t;
    CreateValidEntry(write_e, write_t);

    EXPECT_TRUE(fm.write_entry(write_e, write_t));
    EXPECT_EQ(fm.get_total_vector_count(), 1);
    EXPECT_EQ(fm.get_live_vector_count(), 1);

    DB_entry read_e{};
    std::string read_t;
    EXPECT_TRUE(fm.read_entry(0, read_e, read_t));

    EXPECT_EQ(read_e.flag, 1);
    EXPECT_EQ(std::strncmp(read_e.id, write_e.id, schema::ID_LENGTH), 0);
    EXPECT_EQ(read_t, write_t);
    EXPECT_EQ(read_e.text_length, write_e.text_length);
    EXPECT_EQ(read_e.meta_data_count, schema::META_DATA_KP_PAIRS);

    for (size_t i = 0; i < schema::META_DATA_KP_PAIRS; i++)
    {
        EXPECT_STREQ(read_e.meta_data[i].key, write_e.meta_data[i].key);
        EXPECT_STREQ(read_e.meta_data[i].value, write_e.meta_data[i].value);
    }
    for (size_t i = 0; i < schema::DIMENSIONS; i++)
    {
        EXPECT_FLOAT_EQ(read_e.embeddings[i], write_e.embeddings[i]);
    }
}

TEST_F(FileManagerTest, WriteRead_OutOfOrder_AccessCorrectOffsets)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    const size_t NUM_ENTRIES = 3;

    for (size_t i = 0; i < NUM_ENTRIES; i++)
    {
        DB_entry e{};
        std::string t;
        CreateValidEntry(e, t, i);
        EXPECT_TRUE(fm.write_entry(e, t));
    }

    DB_entry e{};
    std::string t;
    // Read index 2
    EXPECT_TRUE(fm.read_entry(2, e, t));
    EXPECT_EQ(t, "Valid text payload variation 2");
    // Read index 0
    EXPECT_TRUE(fm.read_entry(0, e, t));
    EXPECT_EQ(t, "Valid text payload variation 0");
    // Read index 1
    EXPECT_TRUE(fm.read_entry(1, e, t));
    EXPECT_EQ(t, "Valid text payload variation 1");
}

TEST_F(FileManagerTest, WriteRead_TextBoundaries_ExactMatches)
{
    File_manager fm(entry_path_, text_path_, index_path_);

    // Maximum text length boundary
    DB_entry e_max{};
    std::string t_max;
    CreateValidEntry(e_max, t_max);                    // Setup valid ID, metadata, and embeddings first
    t_max = std::string(schema::TEXT_MAX_LENGTH, 'X'); // Now override the text payload
    e_max.text_length = schema::TEXT_MAX_LENGTH;       // Sync the length
    EXPECT_TRUE(fm.write_entry(e_max, t_max));

    // Minimal text length boundary
    DB_entry e_min{};
    std::string t_min;
    CreateValidEntry(e_min, t_min); // Setup valid fields first
    t_min = "M";                    // Now override the text payload
    e_min.text_length = 1;          // Sync the length
    EXPECT_TRUE(fm.write_entry(e_min, t_min));

    DB_entry read_e{};
    std::string read_t;
    EXPECT_TRUE(fm.read_entry(0, read_e, read_t));
    EXPECT_EQ(read_e.text_length, schema::TEXT_MAX_LENGTH);
    EXPECT_EQ(read_t.size(), schema::TEXT_MAX_LENGTH);

    EXPECT_TRUE(fm.read_entry(1, read_e, read_t));
    EXPECT_EQ(read_e.text_length, 1);
    EXPECT_EQ(read_t.size(), 1);
}

TEST_F(FileManagerTest, WriteRead_ZeroMetadata_PreservesEmptyState)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    DB_entry e{};
    std::string t;
    CreateValidEntry(e, t);
    e.meta_data_count = 0;                            // Override to zero
    std::memset(e.meta_data, 0, sizeof(e.meta_data)); // Clear bytes

    EXPECT_TRUE(fm.write_entry(e, t));

    DB_entry read_e{};
    std::string read_t;
    EXPECT_TRUE(fm.read_entry(0, read_e, read_t));
    EXPECT_EQ(read_e.meta_data_count, 0);
    EXPECT_EQ(read_e.meta_data[0].key[0], '\0');
}

// =============================================================================
// Group 7: read_entry — failure and edge cases
// =============================================================================

TEST_F(FileManagerTest, Read_EmptyDB_ReturnsFalse)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    DB_entry e{};
    std::string t;
    EXPECT_FALSE(fm.read_entry(0, e, t));
}

TEST_F(FileManagerTest, Read_OutOfBounds_ReturnsFalse)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    DB_entry e{};
    std::string t;
    CreateValidEntry(e, t);
    fm.write_entry(e, t); // writes index 0

    EXPECT_FALSE(fm.read_entry(1, e, t)); // Exceeds bounds
}

// =============================================================================
// Group 8: delete_entry
// =============================================================================

TEST_F(FileManagerTest, Delete_ValidEntry_DecrementsLiveCount)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    DB_entry e{};
    std::string t;
    CreateValidEntry(e, t);
    fm.write_entry(e, t);

    EXPECT_TRUE(fm.delete_entry(0));
    EXPECT_EQ(fm.get_live_vector_count(), 0);
    EXPECT_EQ(fm.get_total_vector_count(), 1); // Total is untouched

    DB_entry read_e{};
    std::string read_t;
    EXPECT_FALSE(fm.read_entry(0, read_e, read_t));
}

TEST_F(FileManagerTest, Delete_AlreadyDeleted_IdempotentBehavior)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    DB_entry e{};
    std::string t;
    CreateValidEntry(e, t);
    fm.write_entry(e, t);

    EXPECT_TRUE(fm.delete_entry(0));
    // Contractual behavior: second deletion succeeds safely and does not double-decrement
    EXPECT_TRUE(fm.delete_entry(0));
    EXPECT_EQ(fm.get_live_vector_count(), 0); // No underflow
}

TEST_F(FileManagerTest, Delete_OutOfBounds_ReturnsFalse)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    EXPECT_FALSE(fm.delete_entry(0));
}

TEST_F(FileManagerTest, Delete_TombstonesTextHeap)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    DB_entry e{};
    std::string t;
    CreateValidEntry(e, t);
    fm.write_entry(e, t);

    // Grab the text_offset before deletion
    DB_entry read_e{};
    std::string read_t;
    fm.read_entry(0, read_e, read_t);
    size_t target_offset = read_e.text_offset;

    EXPECT_TRUE(fm.delete_entry(0));

    // Follow-up failed read_text attempt verifies tombstoning
    std::string out_text;
    EXPECT_FALSE(fm.read_text(e.text_length, target_offset, out_text));

    // Explicitly check the text file flag byte
    std::fstream txt(text_path_, std::ios::in | std::ios::binary);
    txt.seekg(target_offset);
    char flag = 1;
    txt.read(&flag, 1);
    EXPECT_EQ(flag, 0) << "Text heap record was not successfully tombstoned!";
}

// =============================================================================
// Group 9: find_by_id
// =============================================================================

TEST_F(FileManagerTest, FindById_Present_ReturnsIndex)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    DB_entry e{};
    std::string t;
    CreateValidEntry(e, t, 0);
    fm.write_entry(e, t);
    CreateValidEntry(e, t, 1);
    fm.write_entry(e, t);

    std::string search_id = "id_1";
    search_id.resize(schema::ID_LENGTH, '\0');

    EXPECT_EQ(fm.find_by_id(search_id), 1);
}

TEST_F(FileManagerTest, FindById_Deleted_ReturnsNegative)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    DB_entry e{};
    std::string t;
    CreateValidEntry(e, t);
    fm.write_entry(e, t);
    fm.delete_entry(0);

    std::string search_id = "id_0";
    search_id.resize(schema::ID_LENGTH, '\0');
    EXPECT_EQ(fm.find_by_id(search_id), -1); // Must ignore tombstoned entry
}

TEST_F(FileManagerTest, FindById_Absent_ReturnsNegative)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    EXPECT_EQ(fm.find_by_id("missing_id"), -1);
}

TEST_F(FileManagerTest, FindById_Oversize_ReturnsNegative)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    std::string oversize_id(schema::ID_LENGTH + 1, 'x');
    EXPECT_EQ(fm.find_by_id(oversize_id), -1); // Short-circuits
}

// =============================================================================
// Group 10: compact()
// =============================================================================

TEST_F(FileManagerTest, Compact_MixedLiveAndDeleted_ReclaimsSpace)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    const size_t TOTAL_ENTRIES = 4;

    for (size_t i = 0; i < TOTAL_ENTRIES; i++)
    {
        DB_entry e{};
        std::string t;
        CreateValidEntry(e, t, i);
        fm.write_entry(e, t);
    }

    fm.delete_entry(1);
    fm.delete_entry(3);

    // Put something in the index file to ensure it survives unaffected
    size_t centroid_count = 2;
    std::vector<float> original_centroids = CreateCentroids(centroid_count);
    fm.write_index_(original_centroids.data(), centroid_count);

    EXPECT_EQ(fm.get_total_vector_count(), 4);
    EXPECT_EQ(fm.get_live_vector_count(), 2);

    EXPECT_TRUE(fm.compact());

    // Post-compaction state
    EXPECT_EQ(fm.get_total_vector_count(), 2);
    EXPECT_EQ(fm.get_live_vector_count(), 2);

    // Locate surviving entries by ID
    std::string id_0 = "id_0";
    id_0.resize(schema::ID_LENGTH, '\0');
    std::string id_2 = "id_2";
    id_2.resize(schema::ID_LENGTH, '\0');
    std::string id_1 = "id_1";
    id_1.resize(schema::ID_LENGTH, '\0'); // deleted

    long new_idx_0 = fm.find_by_id(id_0);
    long new_idx_2 = fm.find_by_id(id_2);

    EXPECT_NE(new_idx_0, -1);
    EXPECT_NE(new_idx_2, -1);
    EXPECT_EQ(fm.find_by_id(id_1), -1); // Truly gone

    // Verify readable data survived
    DB_entry e{};
    std::string t;
    EXPECT_TRUE(fm.read_entry(new_idx_2, e, t));
    EXPECT_EQ(t, "Valid text payload variation 2");

    // Verify Temp files cleaned up
    EXPECT_FALSE(std::filesystem::exists(temp_compact_entry_));
    EXPECT_FALSE(std::filesystem::exists(temp_compact_text_));

    // Verify usability post-compaction
    CreateValidEntry(e, t, 99);
    EXPECT_TRUE(fm.write_entry(e, t));
    EXPECT_EQ(fm.get_total_vector_count(), 3);

    // NEW: Verify index file is untouched and readable after compaction
    std::vector<float> post_compact_centroids = fm.read_index_(centroid_count);
    ASSERT_EQ(post_compact_centroids.size(), original_centroids.size());
    for (size_t i = 0; i < original_centroids.size(); i++)
    {
        EXPECT_FLOAT_EQ(post_compact_centroids[i], original_centroids[i]);
    }
}

TEST_F(FileManagerTest, Compact_AllLive_NoDataLoss)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    DB_entry e{};
    std::string t;
    CreateValidEntry(e, t);
    fm.write_entry(e, t);

    EXPECT_TRUE(fm.compact());
    EXPECT_EQ(fm.get_total_vector_count(), 1);
    EXPECT_TRUE(fm.read_entry(0, e, t)); // Data remains accessible
}

TEST_F(FileManagerTest, Compact_AllDeleted_ClearsDB)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    DB_entry e{};
    std::string t;
    CreateValidEntry(e, t);
    fm.write_entry(e, t);
    fm.delete_entry(0);

    EXPECT_TRUE(fm.compact());
    EXPECT_EQ(fm.get_total_vector_count(), 0);
    EXPECT_EQ(fm.get_live_vector_count(), 0);
}

// =============================================================================
// Group 11: Header persistence sanity
// =============================================================================

TEST_F(FileManagerTest, Persistence_AfterWritesAndDeletes_MaintainsCounts)
{
    {
        File_manager fm(entry_path_, text_path_, index_path_);
        const size_t WRITES = 5;
        for (size_t i = 0; i < WRITES; i++)
        {
            DB_entry e{};
            std::string t;
            CreateValidEntry(e, t, i);
            fm.write_entry(e, t);
        }
        fm.delete_entry(0);
        fm.delete_entry(4);
    }

    File_manager fm_reopened(entry_path_, text_path_, index_path_);
    EXPECT_EQ(fm_reopened.get_total_vector_count(), 5);
    EXPECT_EQ(fm_reopened.get_live_vector_count(), 3);
}

// =============================================================================
// Group 12: Index-file I/O
// =============================================================================

TEST_F(FileManagerTest, IndexFile_RoundTrip_ExactMatches)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    size_t centroid_count = 5;
    std::vector<float> write_centroids = CreateCentroids(centroid_count);

    EXPECT_TRUE(fm.write_index_(write_centroids.data(), centroid_count));

    std::vector<float> read_centroids = fm.read_index_(centroid_count);
    ASSERT_EQ(read_centroids.size(), write_centroids.size());

    // Raw binary I/O ensures perfect equality, not just EXPECT_NEAR
    for (size_t i = 0; i < write_centroids.size(); i++)
    {
        EXPECT_EQ(read_centroids[i], write_centroids[i]);
    }
}

TEST_F(FileManagerTest, IndexFile_GetIndexSize_MatchesActualBytesWritten)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    size_t centroid_count = 5;
    std::vector<float> write_centroids = CreateCentroids(centroid_count);

    fm.write_index_(write_centroids.data(), centroid_count);

    size_t expected_size = centroid_count * schema::DIMENSIONS * sizeof(float);
    EXPECT_EQ(fm.get_index_size(), expected_size);
}

TEST_F(FileManagerTest, IndexFile_IsPopulated_ReflectsStateCorrectly)
{
    File_manager fm(entry_path_, text_path_, index_path_);

    // Freshly created -> false
    EXPECT_FALSE(fm.is_index_populated());

    size_t centroid_count = 1;
    std::vector<float> centroids = CreateCentroids(centroid_count);
    fm.write_index_(centroids.data(), centroid_count);

    // After writing -> true
    EXPECT_TRUE(fm.is_index_populated());
}

TEST_F(FileManagerTest, IndexFile_RequestingMoreThanWritten_FailsSafelyWithEmptyReturn)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    size_t write_count = 2;
    std::vector<float> centroids = CreateCentroids(write_count);
    fm.write_index_(centroids.data(), write_count);

    size_t request_count = 3; // Requesting more than what was written
    std::vector<float> result = fm.read_index_(request_count);

    EXPECT_TRUE(result.empty());
    EXPECT_EQ(result.size(), 0);
}

TEST_F(FileManagerTest, IndexFile_RequestingFewerThanWritten_ReturnsExactPrefix)
{
    File_manager fm(entry_path_, text_path_, index_path_);
    size_t write_count = 4;
    std::vector<float> centroids = CreateCentroids(write_count);
    fm.write_index_(centroids.data(), write_count);

    size_t request_count = 2; // Requesting a strict prefix
    std::vector<float> result = fm.read_index_(request_count);

    ASSERT_EQ(result.size(), request_count * schema::DIMENSIONS);
    for (size_t i = 0; i < result.size(); i++)
    {
        EXPECT_EQ(result[i], centroids[i]);
    }
}

TEST_F(FileManagerTest, IndexFile_RequestingZeroCentroids_IsCleanNoOp)
{
    File_manager fm(entry_path_, text_path_, index_path_);

    // write_index_ with 0 centroids returns true
    EXPECT_TRUE(fm.write_index_(nullptr, 0));
    EXPECT_EQ(fm.get_index_size(), 0);

    // read_index_(0) returns empty vector cleanly
    std::vector<float> result = fm.read_index_(0);
    EXPECT_TRUE(result.empty());
}

TEST_F(FileManagerTest, IndexFile_GetIndexSize_FileSystemError_ReturnsZero)
{
    File_manager fm(entry_path_, text_path_, index_path_);

    // Delete the index file externally to force a filesystem_error in file_size()
    std::filesystem::remove(index_path_);

    // It should catch the exception and return 0
    EXPECT_EQ(fm.get_index_size(), 0);
}