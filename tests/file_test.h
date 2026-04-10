#ifndef FILE_TEST_H
#define FILE_TEST_H

#include <string>
#include <cstdint>

// ─── test groups ────────────────────────────────────────────────────────────

bool test_fresh_create(const std::string &path, uint32_t dims);
bool test_write_and_read(const std::string &path, uint32_t dims);
bool test_persistence(const std::string &path, uint32_t dims);
bool test_delete_and_tombstone(const std::string &path, uint32_t dims);
bool test_find_by_id(const std::string &path, uint32_t dims);
bool test_vacuum_fill(const std::string &path, uint32_t dims);
bool test_schema_mismatch(const std::string &path, uint32_t dims);
bool test_id_truncation(const std::string &path, uint32_t dims);

#endif // MAIN_TEST_H