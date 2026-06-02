#ifndef FILE_MANAGER
#define FILE_MANAGER
#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <cstdint>    // for uint64_t
#include <fstream>    // for file handeling
#include <filesystem> // for verifying if database's existance
#include <algorithm>  // for std::find
#include <iterator>   // for std::distance
#include "types.h"    // for convinient structs

#pragma pack(push, 1) // No hidden padding!, keep the bytes explicit
struct Header
{
    // 8-byte types
    uint64_t live_vector_count;  //  records
    uint64_t total_vector_count; //  includes entries with flag '0' / deleted vectors
    // 4-byte types
    uint32_t dimensions;  //  dims of each vector = 1536/1024/768
    char magic_number[4]; // {'V','D','B','\0'}, required file-format
    // 1-byte types
    uint8_t version;   //  version of database
    uint8_t id_length; //  fixed bytes of "id_name"
    uint8_t kv_length; //  fixed bytes of 'keys' and 'values' for metadata
    uint8_t max_kv;    //  fixed number of key value pairs per record
    // Pad to exactly 32 bytes (32 - 28 = 4 bytes)
    uint8_t padding[4]; //  future-proof
};
#pragma pack(pop)
class File_manager
{
public:
    explicit File_manager(const std::string &path, uint32_t dims, uint8_t id_len, uint8_t kv_len, uint8_t kv_max_pairs);

    // Core operations — all O(1) with fixed record size, except find by id O(n)
    bool write_vector(const std::string &id, const float *data, const Metadata_entry *mdata_arr);
    bool read_vector(uint64_t index, std::string &id_out, float *data_out, Metadata_entry *mdata_arr);
    bool delete_vector(uint64_t index);        // sets tombstone bit
    int64_t find_by_id(const std::string &id); // returns record index or -1

    // Header I/O
    bool flush_header(); // write updated count etc.
    Header read_header();

    // Helpers/getters
    void compact(); // rewrite without tombstoned records
    uint64_t get_live_vector_count() const;
    uint64_t get_total_vector_count() const;
    uint64_t get_record_size() const;

private:
    std::fstream file_;
    Header header_;
    uint64_t record_size_;
    uint64_t get_record_offset(uint64_t index) const;
};

#endif