#pragma once
#include <cstdint>
#include <vector>
/*
    1. DE_entry and Vector both have many overlapping elements, but they are different as they are used for seperate purposes and places.
        o   DB_entry usage is limited to file_manager's internals.
        o   Vector is for vector_store's internals and other parts of the system.
*/
//  ----------------------------------------- Engine-Schema ------------------------------------------
namespace schema
{
    constexpr uint16_t DIMENSIONS = 1024;
    constexpr uint8_t DIMENSIONS_NO_OF_DIGITS = 4;
    constexpr uint8_t ID_LENGTH = 32;
    constexpr uint8_t META_DATA_LENGTH = 32;
    constexpr uint8_t META_DATA_KP_PAIRS = 3;
    constexpr uint16_t TEXT_MAX_LENGTH = 999;
    constexpr uint8_t VERSION = 6;
    constexpr char MAGIC_NUMBER[4] = {'V', 'D', 'B', '\0'};
    constexpr uint8_t MAX_K_SIMILAR = 30;
}

/**
 * @brief Defines system-wide operational constraints populated during startup.
 * @note Instances are passed by constant reference throughout the application lifecycle.
 *
 * @param port Port where engines TCP conncection will be listening from.
 * @param vecdb_entry_file_path Entry database file path
 * @param vecdb_text_file_path Text database file path
 */
struct Config
{
    std::string port = "8080";
    std::string vecdb_entry_file_path = "./data/database_entry.vdb";
    std::string vecdb_text_file_path = "./data/database_text.vdb";
};

//  ---------------------------------------- Data-Base-Schema ----------------------------------------
#pragma pack(push, 1) // No hidden padding! Keep the bytes explicit.

/**
 * @brief 32-byte packed binary schema definition for the persistent database file.
 * @note Strictly packed to 1-byte alignment to prevent cross-platform memory padding inconsistencies.
 *
 * @param live_vector_count  entires with flag '1'
 * @param total_vector_count includes entries with flag '0' / deleted entires
 * @param magic_number       {'V','D','B','\0'}, required file-format
 * @param dimensions         dims of each vector = 1536/1024/768
 * @param version            version of database
 * @param id_length          fixed bytes of "id_name"
 * @param kv_length          fixed bytes of 'keys' and 'values' for metadata
 * @param max_kv             fixed number of key value pairs per record
 * @param padding            future-proofing to ensure 32 bytes size of struct
 */
struct DB_header
{
    // 8-byte types
    uint64_t live_vector_count = 0;
    uint64_t total_vector_count = 0;
    // 4-byte types
    char magic_number[4] = {'V', 'D', 'B', '\0'};
    // 2-byte types
    uint16_t dimensions = schema::DIMENSIONS;
    // 1-byte types
    uint8_t version = schema::VERSION;
    uint8_t id_length = schema::ID_LENGTH;
    uint8_t kv_length = schema::META_DATA_LENGTH;
    uint8_t max_kv = schema::META_DATA_KP_PAIRS;
    // Pad to exactly 32 bytes (32 - 26 = 6 bytes)
    uint8_t padding[6]{};
};
static_assert(sizeof(DB_header) == 32, "[schema.hpp]   |   Header layout mismatch.");

/**
 * @brief Fixed-size character arrays storing a single key-value string pair.
 * @note Maximum length for both key and value is 32 bytes.
 * @warning Does not guarantee null termination if strings exactly match the 32-byte limit.
 */
struct Metadata_entry
{
    char key[32]{};
    char value[32]{};
};
static_assert(sizeof(Metadata_entry) == 64, "[schema.hpp]  |  Key-Value layout mismatch.");

/**
 * @brief Represents a single vector record in the database.
 *
 * @param flag              Status of the entry (e.g., 1 for active/live, 0 for deleted).
 * @param id                Unique 32-bit identifier for the record.
 * @param text_offset       Position in bytes where the text entry gets written from.
 * @param text_length       Length of the text.
 * @param meta_data         Array of key-value pairs storing associated metadata.
 * @param meta_data_count   Meta-data enties passed in this entry.
 * @param embeddings        The high-dimensional vector data array.
 */
struct DB_entry
{
    uint8_t flag = 1;
    char id[schema::ID_LENGTH]{};
    uint64_t text_offset = 0;
    uint16_t text_length = 0;
    Metadata_entry meta_data[schema::META_DATA_KP_PAIRS]{};
    uint8_t meta_data_count = 0;
    float embeddings[schema::DIMENSIONS]{};
};
static_assert(sizeof(DB_entry) == (sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint64_t) + (sizeof(char) * schema::ID_LENGTH) + (schema::DIMENSIONS) * sizeof(float) + (sizeof(Metadata_entry) * schema::META_DATA_KP_PAIRS)), "[schema.hpp]    |    Entry layout mismatch.");

struct Vector
{
    std::string id = std::string(schema::ID_LENGTH, '\0');
    size_t text_offset = 0;
    size_t text_length = 0;
    Metadata_entry meta_data[schema::META_DATA_KP_PAIRS]{};
    uint8_t meta_data_count = 0;
    std::vector<float> embeddings = std::vector(schema::DIMENSIONS, 0.0f);
};
#pragma pack(pop)