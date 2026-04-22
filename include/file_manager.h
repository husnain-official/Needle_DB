#ifndef FILE_MANAGER
#define FILE_MANAGER
#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <cstdint>
#include <fstream>
#include <filesystem>
#pragma pack(push, 1) // No hidden padding!, keep the bytes explicit
struct Header
{
    char magic_number[4];        // {'V','D','B','\0'}, required file-format
    uint8_t version;             //  version of database
    uint32_t dimensions;         //  dims of each vector = 1536/1024
    uint8_t id_length;           //  fixed bytes of "id_name"
    uint64_t live_vector_count;  //  records
    uint64_t total_vector_count; //  includes entries with flag '0' / deleted vectors
    uint8_t padding[6];          // future-proof
};
#pragma pack(pop)
class File_manager
{
public:
    explicit File_manager(const std::string &path, uint32_t dims, uint8_t id_len = 32);

    // Core operations — all O(1) with fixed record size, except find by id O(n)
    bool write_vector(const std::string &id, const float *data);
    bool read_vector(uint64_t index, std::string &id_out, float *data_out);
    bool delete_vector(uint64_t index);        // sets tombstone bit
    int64_t find_by_id(const std::string &id); // returns record index or -1

    // Header I/O
    bool flush_header(); // write updated count etc.
    Header read_header();

    // Helpers/getters
    void compact(); // rewrite without tombstoned records
    uint64_t get_live_vector_count() const;
    uint64_t get_total_vector_count() const;
    uint64_t get_record_size() const; // dims*4 + id_len + 1

private:
    std::fstream file_;
    Header header_;
    uint64_t record_size_;
    uint64_t get_record_offset(uint64_t index) const;
};

#endif
/*
    Header:
        Size is intentionally kept a factor of 8, there was some reason for optimization, search it up later and properly document in
        in the final readme or better in the documentation file





*/