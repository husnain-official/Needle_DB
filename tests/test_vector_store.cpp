#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <cstring>
#include "vector_store.h"
#include "env_config.hpp"
using namespace std;
Config env;
// Helper to spin up a quick 1536-dim vector for testing
vector<float> make_vec(float base_val)
{
    vector<float> v(1536, base_val);
    // Tweak the first few dims so they aren't all perfectly parallel
    v[0] = base_val * 2.5f;
    v[1] = base_val * -1.2f;
    v[2] = base_val * 0.5f;
    return v;
}
// Test:1
void test_normalization()
{
    Vector_store store(env);

    // Edge case 1: Zero vector
    vector<float> zero_vec(1536, 0.0f);
    assert(!store.normalise_vector(zero_vec) && "Zero vector should fail normalization");

    // Normal case
    vector<float> good_vec = make_vec(1.0f);
    assert(store.normalise_vector(good_vec) && "Valid vector should pass");

    cout << " -> Normalization tests passed\n";
}
// Test:2
void test_insert_and_delete()
{
    Vector_store store(env);
    Metadata_entry empty_md[3] = {};

    store.make_entry("vec1", make_vec(1.0f), empty_md);
    store.make_entry("vec2", make_vec(2.0f), empty_md);

    assert(store.get_count() == 2);
    assert(store.get_index_in_ram("vec1").success);

    // Edge case 2: Lookup missing ID
    assert(!store.get_index_in_ram("ghost_vec").success);

    // Delete existing
    assert(store.remove_entry("vec1"));
    assert(store.get_count() == 1);

    // Edge case 3: Delete already deleted/missing vector
    assert(!store.remove_entry("vec1"));

    cout << " -> Insert & Delete tests passed\n";
}
// Test:3
void test_metadata()
{
    Vector_store store(env);

    Metadata_entry md_red[3] = {};
    strncpy(md_red[0].key, "color", 32);
    strncpy(md_red[0].value, "red", 32);

    Metadata_entry md_blue[3] = {};
    strncpy(md_blue[0].key, "color", 32);
    strncpy(md_blue[0].value, "blue", 32);

    store.make_entry("v_red", make_vec(1.0f), md_red);
    store.make_entry("v_blue", make_vec(2.0f), md_blue);

    vector<size_t> matches;

    // Exact match
    assert(store.get_matching_indices(md_red, matches).success);
    assert(matches.size() == 1);
    assert(store.get_id(matches[0]) == "v_red");

    // Edge case 4: Empty filter should return all vectors
    Metadata_entry empty_md[3] = {};
    assert(store.get_matching_indices(empty_md, matches).success);
    assert(matches.size() == 2);

    cout << " -> Metadata filtering tests passed\n";
}

int main()
{
    // -----------------------Configure with .env---------------------------
    bool success = loadServerConfig(".env", env);
    if (!success)
    {
        cout << "Error: Server could not read .env file properly\n";
        return -1;
    }
    cout << "Running Vector_store tests...\n";

    test_normalization();
    test_insert_and_delete();
    test_metadata();

    cout << "All tests passed successfully!\n";
    return 0;
}