#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <cstring>
#include <filesystem>
#include "file_manager.h"
using namespace std;
// Constants mapping to your defined boundaries
const uint32_t TEST_DIMS = 1536;
const uint8_t TEST_ID_LEN = 32;
const uint8_t TEST_KV_LEN = 32;
const uint8_t TEST_MAX_KV = 3;
const string TEST_DB_PATH = "./tests/test_database.vdb";

// Helper to wipe the test DB before/after runs
void cleanup()
{
    if (filesystem::exists(TEST_DB_PATH))
    {
        filesystem::remove(TEST_DB_PATH);
    }
}
// Helper to spin up quick continuous memory blocks
vector<float> make_test_data(float base_val)
{
    return vector<float>(TEST_DIMS, base_val);
}
// Test:1
void test_creation_and_headers()
{
    cleanup();

    {
        // Scope block forces the file stream to close when 'fm' falls out of scope
        File_manager fm(TEST_DB_PATH, TEST_DIMS, TEST_ID_LEN, TEST_KV_LEN, TEST_MAX_KV);
        assert(fm.get_total_vector_count() == 0);
        assert(fm.get_live_vector_count() == 0);
    }

    // Re-open existing database to test if magic number and version logic holds up
    File_manager fm_reload(TEST_DB_PATH, TEST_DIMS, TEST_ID_LEN, TEST_KV_LEN, TEST_MAX_KV);
    assert(fm_reload.get_total_vector_count() == 0);

    cout << " -> File creation & header flush passed\n";
}
// Test:2
void test_write_and_read()
{
    cleanup();
    File_manager fm(TEST_DB_PATH, TEST_DIMS, TEST_ID_LEN, TEST_KV_LEN, TEST_MAX_KV);

    vector<float> vec1 = make_test_data(3.14f);
    Metadata_entry md[TEST_MAX_KV] = {};
    strncpy(md[0].key, "status", TEST_KV_LEN);
    strncpy(md[0].value, "active", TEST_KV_LEN);

    // Write
    assert(fm.write_vector("node_1", vec1.data(), md));
    assert(fm.get_total_vector_count() == 1);

    // Read
    string out_id;
    vector<float> out_vec(TEST_DIMS);
    Metadata_entry out_md[TEST_MAX_KV] = {};

    assert(fm.read_vector(0, out_id, out_vec.data(), out_md));
    assert(out_id == "node_1");
    assert(out_vec[0] == 3.14f); // Check block integrity
    assert(strcmp(out_md[0].key, "status") == 0);
    assert(strcmp(out_md[0].value, "active") == 0);

    // Edge Case: Attempt to read an out-of-bounds record
    assert(!fm.read_vector(1, out_id, out_vec.data(), out_md) && "Should gracefully reject OOB read");

    cout << " -> Write & Read persistence passed\n";
}
// Test:3
void test_find_and_soft_delete()
{
    cleanup();
    File_manager fm(TEST_DB_PATH, TEST_DIMS, TEST_ID_LEN, TEST_KV_LEN, TEST_MAX_KV);

    fm.write_vector("target_vec", make_test_data(1.0f).data(), nullptr);
    fm.write_vector("keep_vec", make_test_data(2.0f).data(), nullptr);

    // Verify linear scan finds correct offsets
    int64_t target_idx = fm.find_by_id("target_vec");
    assert(target_idx == 0);
    assert(fm.find_by_id("keep_vec") == 1);

    // Edge Case: Lookup missing ID or an ID exceeding char limits
    assert(fm.find_by_id("ghost_vec") == -1);
    string long_id(50, 'X');
    assert(fm.find_by_id(long_id) == -1);

    // Toggle the active flag (Soft Delete)
    assert(fm.delete_vector(static_cast<uint64_t>(target_idx)));
    assert(fm.get_live_vector_count() == 1);
    assert(fm.get_total_vector_count() == 2); // Physical size remains unchanged

    // Ensure deleted vector acts invisible to DB
    assert(fm.find_by_id("target_vec") == -1 && "find_by_id should skip tombstoned records");

    string dummy_id;
    vector<float> dummy_data(TEST_DIMS);
    assert(!fm.read_vector(target_idx, dummy_id, dummy_data.data(), nullptr) && "read_vector should skip tombstoned records");

    cout << " -> Linear scan & Tombstone flag tests passed\n";
}

int main()
{
    cout << "Running File_manager tests...\n";

    test_creation_and_headers();
    test_write_and_read();
    test_find_and_soft_delete();

    cleanup(); // Clean up binary files from the working directory
    cout << "All disk I/O tests passed successfully!\n";
    return 0;
}