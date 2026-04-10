#ifndef FILE_MANAGER
#define FILE_MANAGER
#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <cstdint>
#include <fstream>
#include <filesystem>
#pragma pack(push, 1) // No hidden padding!
struct Header
{
    char magic_number[4];  // {'V','D','B','\0'}, required file-format
    uint8_t version;       //  version of database
    uint32_t dimensions;   //  dims of each vector = 1536
    uint8_t id_length;     //  fixed bytes of "id_name"
    uint64_t vector_count; //  records
    uint8_t padding[6];    // future-proof
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

    // Maintenance
    void compact(); // rewrite without tombstoned records
    uint64_t vector_count() const;
    uint64_t record_size() const; // dims*4 + id_len + 1

private:
    std::fstream file_;
    Header header_;
    uint64_t record_size_;
    uint64_t record_offset(uint64_t index) const;
};

#endif