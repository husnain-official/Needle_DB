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
/**
 * @brief 32-byte packed binary schema definition for the persistent database file.
 * @note Strictly packed to 1-byte alignment to prevent cross-platform memory padding inconsistencies.
 */
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
    /**
     * @brief Initializes the binary file handler and validates schema compatibility.
     * @param path Target filesystem path for the persistent database file
     * @param dims Expected embedding dimensionality to enforce
     * @param id_len Fixed byte length allocated for string identifiers
     * @param kv_len Fixed byte length allocated for metadata strings
     * @param kv_max_pairs Maximum allowed metadata key-value pairs per record
     * @warning Throws std::runtime_error if an existing file schema mismatches configuration
     */
    explicit File_manager(const std::string &path, uint32_t dims, uint8_t id_len, uint8_t kv_len, uint8_t kv_max_pairs);

    // Core operations — all O(1) with fixed record size, except find by id O(n)
    /**
     * @brief Appends a new contiguous vector record to the EOF.
     * @param id String identifier to persist, truncated to configured fixed length
     * @param data Raw contiguous floating-point sequence matching configured dimensions
     * @param mdata_arr Array of metadata key-value pairs or nullptr
     * @return True upon successful disk flush, false otherwise
     * @warning Does not verify if the string identifier already exists on disk
     */
    bool write_vector(const std::string &id, const float *data, const Metadata_entry *mdata_arr);
    /**
     * @brief Extracts a specific vector record from disk into active memory.
     * @param index Target sequential record offset
     * @param id_out Output string reference populated with the extracted identifier
     * @param data_out Pre-allocated array to receive raw embedding floats
     * @param mdata_arr Pre-allocated array or nullptr to bypass metadata extraction
     * @return True on success, false if the record contains a tombstone flag or exceeds bounds
     */
    bool read_vector(uint64_t index, std::string &id_out, float *data_out, Metadata_entry *mdata_arr);
    /**
     * @brief Marks a persistent disk record as soft-deleted via a tombstone flag.
     * @param index Target sequential record offset
     * @return True if the tombstone bit successfully writes, false on failure
     * @warning Does not reclaim physical disk space
     */ 
    bool delete_vector(uint64_t index);
    /**
     * @brief Locates the logical disk offset of a specific string identifier.
     * @param id Target string identifier to search
     * @return Sequential record index, or negative one if unfound
     * @warning Executes an unoptimized O(N) linear scan across the binary file
     */
    int64_t find_by_id(const std::string &id);

    // Header I/O
    /**
     * @brief Synchronizes the cached header structure to the beginning of the binary file.
     * @return True if the disk write succeeds, false otherwise
     */
    bool flush_header();
    /**
     * @brief Extracts the 32-byte binary schema definition from the file start.
     * @return Fully populated header struct
     * @warning Manipulates the internal file stream cursor position
     */
    Header read_header();

    // Helpers/getters
    /**
     * @brief Rewrites the persistent binary file to purge all soft-deleted records.
     * @warning Blocks all active I/O operations and requires additional temporary disk space
     */
    void compact();
    uint64_t get_live_vector_count() const;
    uint64_t get_total_vector_count() const;
    uint64_t get_record_size() const;

private:
    std::fstream file_;
    Header header_;
    uint64_t record_size_;
    /**
     * @brief Computes the absolute physical byte offset for a specific sequential record.
     * @param index Zero-based sequential target record number
     * @return Computed byte boundary relative to the beginning of the file
     * @warning Does not validate if the computed offset exceeds actual physical file boundaries
     * @note Assumes the fixed-size binary header immediately precedes all contiguous data records
     */
    uint64_t get_record_offset(uint64_t index) const;
};

#endif