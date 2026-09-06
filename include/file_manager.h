#ifndef FILE_MANAGER
#define FILE_MANAGER
#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <cstdint>    // for uint64_t
#include <fstream>    // for file handeling
#include <filesystem> // for verifying database's existance
#include <algorithm>  // for std::find
#include <iterator>   // for std::distance
#include "types.h"    // for convinient structs
#include "schema.hpp" // for direct schema-enforcing

class File_manager
{
public:
    /**
     * @brief Initializes the binary file handler and validates schema compatibility.
     * @param path Target filesystem path for the persistent database file
     * @warning Throws std::runtime_error if an existing file schema mismatches configuration
     * @warning Will recreate both databases, if one or both are missing.
     */
    explicit File_manager(const std::string &path, const std::string &text_path, const std::string &index_path);

    // --- Core operations — all O(1) with fixed record size, except find by id O(n) and compact O(n)
    /**
     * @brief Appends a new contiguous entry record to the EOF.
     * @param entry DB_Entry object to be writte to Disk
     * @param text Text of the entry, to be stored in a seperate file
     * @return True upon successful disk flush, false otherwise
     * @warning Does not verify if the string identifier already exists on disk
     */
    bool write_entry(DB_entry &entry, std::string &text);
    /**
     * @brief Extracts a specific vector record from disk into active memory.
     * @param index Target sequential record offset
     * @param entry Output DB_Entry data structure fully populated
     * @param text Output text assosiated with the entry at index
     * @return True on success, false if the record contains a tombstone flag or exceeds bounds
     */
    bool read_entry(size_t index, DB_entry &entry, std::string &text);
    bool read_text(const size_t text_length, const size_t text_offset, std::string &text);
    /**
     * @brief Marks a persistent disk record as soft-deleted via a tombstone flag.
     * @param index Target sequential record offset
     * @return True if the tombstone bit successfully writes, false on failure
     * @warning Does not reclaim physical disk space
     */
    bool delete_entry(const size_t index);
    /**
     * @brief Locates the logical disk offset of a specific string identifier.
     * @param id Target string identifier to search
     * @return Sequential record index, or negative one if unfound
     * @warning Executes an unoptimized O(N) linear scan across the binary file
     */
    int64_t find_by_id(const std::string &id);
    /**
     * @brief Rewrites the persistent binary file to purge all soft-deleted records.
     * @warning Blocks all active I/O operations and requires additional temporary disk space
     * @warning Does not include safety for cases when power/engine stops mid this function's working, All data will be corrupted.
     * @return True if the disk update succeeds, false otherwise
     */
    bool compact();
    std::vector<float> read_index_(const size_t centroid_numbers);
    bool write_index_(const float *centroids_ptr, const size_t centroid_numbers);
    size_t get_index_size();

    // --- Header I/O
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
    DB_header read_header();

    // --- Helpers/getters
    size_t get_live_vector_count() const;
    size_t get_total_vector_count() const;
    bool is_index_populated() const;

private:
    std::fstream file_;
    std::fstream text_file_;
    std::fstream index_file_;
    const std::string path_;
    const std::string text_file_path_;
    const std::string index_file_path_;
    DB_header header_;
    size_t record_size_;
    /**
     * @brief Computes the absolute physical byte offset for a specific sequential record.
     * @param index Zero-based sequential target record number
     * @return Computed byte boundary relative to the beginning of the file
     * @warning Does not validate if the computed offset exceeds actual physical file boundaries
     * @note Assumes the fixed-size binary header immediately precedes all contiguous data records
     */
    size_t get_record_offset(size_t index) const;
};

#endif