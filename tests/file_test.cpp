#include "file_manager.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>
#include <filesystem>

// ─── helpers ────────────────────────────────────────────────────────────────

static std::vector<float> make_embedding(uint32_t dims, float fill_value)
{
    return std::vector<float>(dims, fill_value);
}

static bool embeddings_equal(const float *a, const float *b, uint32_t dims, float eps = 1e-6f)
{
    for (uint32_t i = 0; i < dims; ++i)
        if (std::fabs(a[i] - b[i]) > eps)
            return false;
    return true;
}

static void pass(const char *name) { std::cout << "  [PASS]  " << name << "\n"; }
static void fail(const char *name) { std::cout << "  [FAIL]  " << name << "\n"; }

#define CHECK(name, expr)       \
    do                          \
    {                           \
        if (expr)               \
            pass(name);         \
        else                    \
        {                       \
            fail(name);         \
            all_passed = false; \
        }                       \
    } while (0)

// ─── test groups ────────────────────────────────────────────────────────────

bool test_fresh_create(const std::string &path, uint32_t dims)
{
    bool all_passed = true;
    std::cout << "\n[1] Fresh database creation\n";

    File_manager db(path, dims);
    CHECK("vector_count is 0 on fresh file", db.vector_count() == 0);
    CHECK("record_size is correct",
          db.record_size() == 1 + 32 + sizeof(float) * dims); // flag + id_len(32) + embeddings

    Header h = db.read_header();
    CHECK("magic number", std::strncmp(h.magic_number, "VDB", 3) == 0);
    CHECK("version is 1", h.version == 1);
    CHECK("dimensions match", h.dimensions == dims);
    CHECK("id_length default 32", h.id_length == 32);

    return all_passed;
}

bool test_write_and_read(const std::string &path, uint32_t dims)
{
    bool all_passed = true;
    std::cout << "\n[2] Write then read back\n";

    File_manager db(path, dims);
    auto emb_a = make_embedding(dims, 1.5f);
    auto emb_b = make_embedding(dims, 2.5f);

    bool w1 = db.write_vector("alpha", emb_a.data());
    bool w2 = db.write_vector("beta", emb_b.data());
    CHECK("write_vector returns true (1)", w1);
    CHECK("write_vector returns true (2)", w2);
    CHECK("vector_count == 2", db.vector_count() == 2);

    std::string id_out;
    std::vector<float> data_out(dims, 0.f);

    bool r1 = db.read_vector(0, id_out, data_out.data());
    CHECK("read_vector index 0 returns true", r1);
    CHECK("id round-trips correctly", id_out == "alpha");
    CHECK("embeddings round-trip correctly",
          embeddings_equal(emb_a.data(), data_out.data(), dims));

    bool r2 = db.read_vector(1, id_out, data_out.data());
    CHECK("read_vector index 1 returns true", r2);
    CHECK("id round-trips correctly (2)", id_out == "beta");
    CHECK("embeddings round-trip correctly (2)",
          embeddings_equal(emb_b.data(), data_out.data(), dims));

    return all_passed;
}

bool test_persistence(const std::string &path, uint32_t dims)
{
    bool all_passed = true;
    std::cout << "\n[3] Persistence across re-open\n";

    // Write in one instance, read in another
    {
        File_manager db(path, dims);
        auto emb = make_embedding(dims, 9.9f);
        db.write_vector("persist-me", emb.data());
    }
    {
        File_manager db(path, dims);
        CHECK("vector_count survives re-open", db.vector_count() == 1);

        std::string id_out;
        std::vector<float> data_out(dims, 0.f);
        db.read_vector(0, id_out, data_out.data());

        auto emb = make_embedding(dims, 9.9f);
        CHECK("id survives re-open", id_out == "persist-me");
        CHECK("embeddings survive re-open",
              embeddings_equal(emb.data(), data_out.data(), dims));
    }

    return all_passed;
}

bool test_delete_and_tombstone(const std::string &path, uint32_t dims)
{
    bool all_passed = true;
    std::cout << "\n[4] Delete / tombstone\n";

    File_manager db(path, dims);
    auto emb_a = make_embedding(dims, 1.f);
    auto emb_b = make_embedding(dims, 2.f);
    db.write_vector("to-delete", emb_a.data());
    db.write_vector("survivor", emb_b.data());

    bool del = db.delete_vector(0);
    CHECK("delete_vector returns true", del);

    // Deleted slot must return false
    std::string id_out;
    std::vector<float> data_out(dims, 0.f);
    bool r = db.read_vector(0, id_out, data_out.data());
    CHECK("read_vector on tombstone returns false", !r);

    // Survivor must still be readable
    bool r2 = db.read_vector(1, id_out, data_out.data());
    CHECK("survivor still readable", r2);
    CHECK("survivor id correct", id_out == "survivor");

    // Out-of-range read
    bool r3 = db.read_vector(999, id_out, data_out.data());
    CHECK("out-of-range index returns false", !r3);

    return all_passed;
}

bool test_find_by_id(const std::string &path, uint32_t dims)
{
    bool all_passed = true;
    std::cout << "\n[5] find_by_id\n";

    File_manager db(path, dims);
    auto emb = make_embedding(dims, 0.5f);
    db.write_vector("needle", emb.data());
    db.write_vector("haystack", emb.data());
    db.write_vector("needle2", emb.data());

    CHECK("find existing id", db.find_by_id("needle") == 0);
    CHECK("find existing id (2)", db.find_by_id("haystack") == 1);
    CHECK("find missing id", db.find_by_id("ghost") == -1);

    // Delete needle, it must not be found anymore
    db.delete_vector(0);
    CHECK("find_by_id skips tombstone", db.find_by_id("needle") == -1);

    return all_passed;
}

bool test_vacuum_fill(const std::string &path, uint32_t dims)
{
    bool all_passed = true;
    std::cout << "\n[6] Vacuum fill (tombstone reuse)\n";

    File_manager db(path, dims);
    auto emb_old = make_embedding(dims, 1.f);
    auto emb_new = make_embedding(dims, 7.f);

    db.write_vector("slot0", emb_old.data());
    db.write_vector("slot1", emb_old.data());
    db.delete_vector(0); // tombstone slot 0

    uint64_t count_before = db.vector_count();
    db.write_vector("reused", emb_new.data()); // should land in slot 0
    uint64_t count_after = db.vector_count();

    CHECK("vector_count does NOT grow on reuse", count_before == count_after);
    CHECK("find_by_id finds reused entry at 0", db.find_by_id("reused") == 0);

    std::string id_out;
    std::vector<float> data_out(dims, 0.f);
    db.read_vector(0, id_out, data_out.data());
    CHECK("reused slot has correct id", id_out == "reused");
    CHECK("reused slot has correct embedding",
          embeddings_equal(emb_new.data(), data_out.data(), dims));

    return all_passed;
}

bool test_schema_mismatch(const std::string &path, uint32_t dims)
{
    bool all_passed = true;
    std::cout << "\n[7] Schema mismatch on re-open\n";

    {
        File_manager db(path, dims);
    } // create file with dims

    bool threw = false;
    try
    {
        File_manager db(path, dims + 1);
    } // wrong dims
    catch (const std::runtime_error &)
    {
        threw = true;
    }
    CHECK("throws on dimension mismatch", threw);

    return all_passed;
}

bool test_id_truncation(const std::string &path, uint32_t dims)
{
    bool all_passed = true;
    std::cout << "\n[8] ID longer than id_length is silently truncated\n";

    File_manager db(path, dims, /*id_len=*/8);
    auto emb = make_embedding(dims, 3.f);

    db.write_vector("123456789_toolong", emb.data()); // 17 chars, only 8 stored

    std::string id_out;
    std::vector<float> data_out(dims);
    db.read_vector(0, id_out, data_out.data());
    CHECK("id is capped at id_length", id_out == "12345678");

    return all_passed;
}

// ─── runner ─────────────────────────────────────────────────────────────────

// int main()
// {
// const std::string BASE = "./test_db_";
// const uint32_t DIMS = 8; // tiny dims — fast, easy to reason about
// int failed = 0;
// // Each test gets its own fresh file
// auto run = [&](const char *tag, auto fn)
// {
//     std::string p = BASE + tag + ".vdb";
//     std::filesystem::remove(p); // always start clean
//     if (!fn(p, DIMS))
//         ++failed;
// };
// run("fresh", test_fresh_create);
// run("rw", test_write_and_read);
// run("persist", test_persistence);
// run("delete", test_delete_and_tombstone);
// run("find", test_find_by_id);
// run("vacuum", test_vacuum_fill);
// run("schema", test_schema_mismatch);
// run("truncate", test_id_truncation);
// std::cout << "\n────────────────────────────────\n";
// if (failed == 0)
//     std::cout << "All tests passed.\n";
// else
//     std::cout << failed << " test group(s) had failures.\n";
// return failed == 0 ? 0 : 1;
// }