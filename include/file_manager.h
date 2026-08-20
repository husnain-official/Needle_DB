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
     */
    explicit File_manager(const std::string &path, const std::string &text_path);

    // Core operations — all O(1) with fixed record size, except find by id O(n)
    /**
     * @brief Appends a new contiguous vector record to the EOF.
     * @param id String identifier to persist, truncated to configured fixed length
     * @param data Raw contiguous floating-point sequence matching configured dimensions
     * @param mdata_arr Array of metadata key-value pairs or nullptr
     * @return True upon successful disk flush, false otherwise
     * @warning Does not verify if the string identifier already exists on disk
     */
    bool write_entry(const DB_entry &, std::string &);
    /**
     * @brief Extracts a specific vector record from disk into active memory.
     * @param index Target sequential record offset
     * @param id_out Output string reference populated with the extracted identifier
     * @param data_out Pre-allocated array to receive raw embedding floats
     * @param mdata_arr Pre-allocated array or nullptr to bypass metadata extraction
     * @return True on success, false if the record contains a tombstone flag or exceeds bounds
     */
    bool read_entry(size_t index, DB_entry &, std::string &);
    /**
     * @brief Marks a persistent disk record as soft-deleted via a tombstone flag.
     * @param index Target sequential record offset
     * @return True if the tombstone bit successfully writes, false on failure
     * @warning Does not reclaim physical disk space
     */
    bool delete_entry(size_t);
    /**
     * @brief Locates the logical disk offset of a specific string identifier.
     * @param id Target string identifier to search
     * @return Sequential record index, or negative one if unfound
     * @warning Executes an unoptimized O(N) linear scan across the binary file
     */
    int64_t find_by_id(const std::string &);
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
    DB_header read_header();

    // Helpers/getters
    /**
     * @brief Rewrites the persistent binary file to purge all soft-deleted records.
     * @warning Blocks all active I/O operations and requires additional temporary disk space
     * @return True if the disk update succeeds, false otherwise
     */
    bool compact();
    size_t get_live_vector_count() const;
    size_t get_total_vector_count() const;

private:
    std::fstream file_;
    std::fstream text_file_;
    const std::string path_;
    const std::string text_file_path_;
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