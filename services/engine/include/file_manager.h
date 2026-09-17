/**
 * @file file_manager.h
 * @brief Persistent storage engine handling binary disk operations, file schema validation, and database compaction.
 */
#pragma once
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
/**
 * @brief Utility class responsible for persistent storage, retrieval, and disk-level management of binary database records, text payloads, and index topologies.
 */
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
    // --- Entry-DB ---
    /**
     * @brief Appends a new persistent database record and its associated text payload to the ends of their respective files.
     * @param entry Target database record structure to be written to disk.
     * @param text Associated text payload string to be stored.
     * @return True upon successful disk flush, false if any file stream encounters an error.
     * @note Automatically calculates and updates the text_offset field within the provided entry structure before writing.
     * @warning Does not verify if the string identifier already exists on disk.
     */
    bool write_entry(DB_entry &entry, std::string &text);

    /**
     * @brief Extracts a specific vector record and its associated text payload from disk into active memory.
     * @param index Zero-based sequential target record number.
     * @param entry Output record structure populated upon successful extraction.
     * @param text Output string populated with the extracted text payload.
     * @return True upon successful extraction, false if the record contains a tombstone flag, exceeds physical bounds, or encounters a stream error.
     */
    bool read_entry(size_t index, DB_entry &entry, std::string &text);

    /**
     * @brief Marks a persistent database record and its associated text payload as soft-deleted via tombstone flags.
     * @param index Zero-based sequential target record number.
     * @return True upon successful disk flush, false if the record exceeds physical bounds or encounters a stream error.
     * @note Silently returns true without modifying disk if the target record is already marked as deleted.
     * @warning Does not reclaim physical disk space.
     */
    bool delete_entry(const size_t index);

    /**
     * @brief Locates the logical disk offset of a specific string identifier.
     * @param id Target string identifier to search.
     * @return Zero-based sequential target record number, or negative one if unfound.
     * @warning Executes an unoptimized linear O(N) scan across the binary file.
     */
    int64_t find_by_id(const std::string &id);

    /**
     * @brief Rewrites the persistent entry and text database files to purge all soft-deleted records.
     * @return True upon successful disk update and file replacement, false if an exception occurs.
     * @warning Blocks all active I/O operations and requires additional temporary disk space.
     * @warning Lacks atomic fail-safes; an unexpected shutdown during execution will cause irreversible data corruption.
     */
    bool compact();

    /**
     * @brief Evaluates whether the volume of soft-deleted records exceeds the configured schema threshold for automatic compaction.
     * @return True if the database qualifies for compaction, false otherwise or if the database is empty.
     */
    bool compact_allowed();

    // --- Header I/O ---
    /**
     * @brief Synchronizes the cached header structure to the beginning of the persistent entry database file.
     * @return True upon successful disk flush, false if the file stream encounters an error.
     */
    bool flush_header();

    /**
     * @brief Extracts the binary schema definition and record counts from the beginning of the persistent entry database file.
     * @return Fully populated header structure extracted from disk.
     * @warning Manipulates the internal file stream cursor position.
     */
    DB_header read_header();

    // --- Text-DB ---
    /**
     * @brief Extracts a specific text payload from the persistent text database file using an absolute byte offset.
     * @param text_length Byte length of the associated text payload.
     * @param text_offset Absolute byte offset in the text database file where the associated payload begins.
     * @param text Output string populated with the extracted text payload.
     * @return True upon successful extraction, false if the record contains a tombstone flag, exceeds physical bounds, or encounters a stream error.
     */
    bool read_text(const size_t text_length, const size_t text_offset, std::string &text);

    // --- Index-DB ---
    /**
     * @brief Extracts the flattened IVF centroid topology from the persistent index database file.
     * @param centroid_numbers Target number of k-means clusters to extract.
     * @return Transient floating-point sequence containing the centroid coordinates, or an empty sequence upon failure.
     * @note Skips the initial 8-byte build tracker offset to read directly from the data block.
     */
    std::vector<float> read_index_(const size_t centroid_numbers);

    /**
     * @brief Extracts the total database record count recorded during the previous IVF index build.
     * @return Extracted record count, or zero if the file stream encounters an error.
     * @note Reads directly from the initial 8-byte offset of the persistent index database file.
     */
    uint64_t read_index_last_build();

    /**
     * @brief Synchronizes the flattened IVF centroid topology to the persistent index database file.
     * @param centroids_ptr Raw pointer to a contiguous block of floating-point centroid data.
     * @param centroid_numbers Target number of k-means clusters to write.
     * @return True upon successful disk flush, false if the file stream encounters an error.
     * @note Skips the initial 8-byte build tracker offset to write directly to the data block.
     */
    bool write_index_(const float *centroids_ptr, const size_t centroid_numbers);

    /**
     * @brief Synchronizes the active database record count to the persistent index database file to track index staleness.
     * @param entry_number Count of all records appended to the database at the time of the build.
     * @return True upon successful disk flush, false if the file stream encounters an error.
     * @note Writes directly to the initial 8-byte offset of the persistent index database file.
     */
    bool write_index_last_build(uint64_t);

    /**
     * @brief Calculates the byte size of the centroid data stored in the persistent index database file.
     * @return Computed byte size excluding the initial 8-byte build tracker offset, or zero if empty or an error occurs.
     */
    size_t get_index_size();

    // --- Helpers/getters ---
    /**
     * @brief Retrieves the current count of active records residing in the database.
     * @return Count of active records cached in the memory header.
     */
    size_t get_live_vector_count() const;

    /**
     * @brief Retrieves the total count of all records appended to the file, including soft-deleted tombstones.
     * @return Count of all records cached in the memory header.
     */
    size_t get_total_vector_count() const;

    /**
     * @brief Verifies whether the persistent index database file contains any written data.
     * @return True if the file size is greater than zero, false otherwise.
     */
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
     * @brief Computes the absolute physical byte offset for a specific sequential record within the persistent entry database file.
     * @param index Zero-based sequential target record number.
     * @return Computed byte boundary relative to the beginning of the entry database file.
     * @note Assumes the fixed-size binary header immediately precedes all contiguous data records.
     * @warning Does not validate if the computed offset exceeds actual physical file boundaries.
     */
    size_t get_record_offset(size_t index) const;
};
